/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gb/mbc.h>

#include <mgba/core/interface.h>
#include <mgba/internal/lr35902/lr35902.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/memory.h>
#include <mgba-util/vfs.h>

mLOG_DEFINE_CATEGORY(GB_MBC, "GB MBC", "gb.mbc");

static void _GBMBCNone(struct GB* gb, uint16_t address, uint8_t value) {
	UNUSED(gb);
	UNUSED(address);
	UNUSED(value);

	mLOG(GB_MBC, GAME_ERROR, "Wrote to invalid MBC");
}

static void _GBMBC1(struct GB*, uint16_t address, uint8_t value);
static void _GBMBC2(struct GB*, uint16_t address, uint8_t value);
static void _GBMBC3(struct GB*, uint16_t address, uint8_t value);
static void _GBMBC5(struct GB*, uint16_t address, uint8_t value);
static void _GBMBC6(struct GB*, uint16_t address, uint8_t value);
static void _GBMBC7(struct GB*, uint16_t address, uint8_t value);
static void _GBHuC3(struct GB*, uint16_t address, uint8_t value);
static void _GBPocketCam(struct GB* gb, uint16_t address, uint8_t value);
static void _GBTAMA5(struct GB* gb, uint16_t address, uint8_t value);

static uint8_t _GBMBC7Read(struct GBMemory*, uint16_t address);
static void _GBMBC7Write(struct GBMemory* memory, uint16_t address, uint8_t value);

static uint8_t _GBTAMA5Read(struct GBMemory*, uint16_t address);

static uint8_t _GBPocketCamRead(struct GBMemory*, uint16_t address);

void GBMBCSwitchBank(struct GB* gb, int bank) {
	size_t bankStart = bank * GB_SIZE_CART_BANK0;
	if (bankStart + GB_SIZE_CART_BANK0 > gb->memory.romSize) {
		mLOG(GB_MBC, GAME_ERROR, "Attempting to switch to an invalid ROM bank: %0X", bank);
		bankStart &= (gb->memory.romSize - 1);
		bank = bankStart / GB_SIZE_CART_BANK0;
		if (!bank) {
			++bank;
		}
	}
	gb->memory.romBank = &gb->memory.rom[bankStart];
	gb->memory.currentBank = bank;
	if (gb->cpu->pc < GB_BASE_VRAM) {
		gb->cpu->memory.setActiveRegion(gb->cpu, gb->cpu->pc);
	}
}

void GBMBCSwitchBank0(struct GB* gb, int bank) {
	size_t bankStart = bank * GB_SIZE_CART_BANK0 << gb->memory.mbcState.mbc1.multicartStride;
	if (bankStart + GB_SIZE_CART_BANK0 > gb->memory.romSize) {
		mLOG(GB_MBC, GAME_ERROR, "Attempting to switch to an invalid ROM bank: %0X", bank);
		bankStart &= (gb->memory.romSize - 1);
	}
	gb->memory.romBase = &gb->memory.rom[bankStart];
	if (gb->cpu->pc < GB_SIZE_CART_BANK0) {
		gb->cpu->memory.setActiveRegion(gb->cpu, gb->cpu->pc);
	}
}

static bool _isMulticart(const uint8_t* mem) {
	bool success = true;
	struct VFile* vf;

	vf = VFileFromConstMemory(&mem[GB_SIZE_CART_BANK0 * 0x10], 1024);
	success = success && GBIsROM(vf);
	vf->close(vf);

	vf = VFileFromConstMemory(&mem[GB_SIZE_CART_BANK0 * 0x20], 1024);
	success = success && GBIsROM(vf);
	vf->close(vf);

	return success;
}

void GBMBCSwitchSramBank(struct GB* gb, int bank) {
	size_t bankStart = bank * GB_SIZE_EXTERNAL_RAM;
	if (bankStart + GB_SIZE_EXTERNAL_RAM > gb->sramSize) {
		mLOG(GB_MBC, GAME_ERROR, "Attempting to switch to an invalid RAM bank: %0X", bank);
		bankStart &= (gb->sramSize - 1);
		bank = bankStart / GB_SIZE_EXTERNAL_RAM;
	}
	gb->memory.sramBank = &gb->memory.sram[bankStart];
	gb->memory.sramCurrentBank = bank;
}

void GBMBCInit(struct GB* gb) {
	const struct GBCartridge* cart = (const struct GBCartridge*) &gb->memory.rom[0x100];
	if (gb->memory.rom) {
		switch (cart->ramSize) {
		case 0:
			gb->sramSize = 0;
			break;
		case 1:
			gb->sramSize = 0x800;
			break;
		default:
		case 2:
			gb->sramSize = 0x2000;
			break;
		case 3:
			gb->sramSize = 0x8000;
			break;
		}

		if (gb->memory.mbcType == GB_MBC_AUTODETECT) {
			switch (cart->type) {
			case 0:
			case 8:
			case 9:
				gb->memory.mbcType = GB_MBC_NONE;
				break;
			case 1:
			case 2:
			case 3:
				gb->memory.mbcType = GB_MBC1;
				if (gb->memory.romSize >= GB_SIZE_CART_BANK0 * 0x31 && _isMulticart(gb->memory.rom)) {
					gb->memory.mbcState.mbc1.multicartStride = 4;
				} else {
					gb->memory.mbcState.mbc1.multicartStride = 5;
				}
				break;
			case 5:
			case 6:
				gb->memory.mbcType = GB_MBC2;
				break;
			case 0x0F:
			case 0x10:
				gb->memory.mbcType = GB_MBC3_RTC;
				break;
			case 0x11:
			case 0x12:
			case 0x13:
				gb->memory.mbcType = GB_MBC3;
				break;
			default:
				mLOG(GB_MBC, WARN, "Unknown MBC type: %02X", cart->type);
				// Fall through
			case 0x19:
			case 0x1A:
			case 0x1B:
				gb->memory.mbcType = GB_MBC5;
				break;
			case 0x1C:
			case 0x1D:
			case 0x1E:
				gb->memory.mbcType = GB_MBC5_RUMBLE;
				break;
			case 0x20:
				gb->memory.mbcType = GB_MBC6;
				break;
			case 0x22:
				gb->memory.mbcType = GB_MBC7;
				break;
			case 0xFC:
				gb->memory.mbcType = GB_POCKETCAM;
				break;
			case 0xFD:
				gb->memory.mbcType = GB_TAMA5;
				break;
			case 0xFE:
				gb->memory.mbcType = GB_HuC3;
				break;
			case 0xFF:
				gb->memory.mbcType = GB_HuC1;
				break;
			}
		}
	} else {
		gb->memory.mbcType = GB_MBC_NONE;
	}
	gb->memory.mbcRead = NULL;
	switch (gb->memory.mbcType) {
	case GB_MBC_NONE:
		gb->memory.mbcWrite = _GBMBCNone;
		break;
	case GB_MBC1:
		gb->memory.mbcWrite = _GBMBC1;
		break;
	case GB_MBC2:
		gb->memory.mbcWrite = _GBMBC2;
		gb->sramSize = 0x200;
		break;
	case GB_MBC3:
		gb->memory.mbcWrite = _GBMBC3;
		break;
	default:
		mLOG(GB_MBC, WARN, "Unknown MBC type: %02X", cart->type);
		// Fall through
	case GB_MBC5:
		gb->memory.mbcWrite = _GBMBC5;
		break;
	case GB_MBC6:
		mLOG(GB_MBC, WARN, "unimplemented MBC: MBC6");
		gb->memory.mbcWrite = _GBMBC6;
		break;
	case GB_MBC7:
		gb->memory.mbcWrite = _GBMBC7;
		gb->memory.mbcRead = _GBMBC7Read;
		gb->sramSize = 0x100;
		break;
	case GB_MMM01:
		mLOG(GB_MBC, WARN, "unimplemented MBC: MMM01");
		gb->memory.mbcWrite = _GBMBC1;
		break;
	case GB_HuC1:
		mLOG(GB_MBC, WARN, "unimplemented MBC: HuC-1");
		gb->memory.mbcWrite = _GBMBC1;
		break;
	case GB_HuC3:
		gb->memory.mbcWrite = _GBHuC3;
		break;
	case GB_TAMA5:
		mLOG(GB_MBC, WARN, "unimplemented MBC: TAMA5");
		memset(gb->memory.rtcRegs, 0, sizeof(gb->memory.rtcRegs));
		gb->memory.mbcWrite = _GBTAMA5;
		gb->memory.mbcRead = _GBTAMA5Read;
		gb->sramSize = 0x20;
		break;
	case GB_MBC3_RTC:
		memset(gb->memory.rtcRegs, 0, sizeof(gb->memory.rtcRegs));
		gb->memory.mbcWrite = _GBMBC3;
		break;
	case GB_MBC5_RUMBLE:
		gb->memory.mbcWrite = _GBMBC5;
		break;
	case GB_POCKETCAM:
		gb->memory.mbcWrite = _GBPocketCam;
		gb->memory.mbcRead = _GBPocketCamRead;
		break;
	}

	gb->memory.currentBank = 1;
	gb->memory.sramCurrentBank = 0;
	gb->memory.sramAccess = false;
	gb->memory.rtcAccess = false;
	gb->memory.activeRtcReg = 0;
	gb->memory.rtcLatched = false;
	memset(&gb->memory.rtcRegs, 0, sizeof(gb->memory.rtcRegs));

	GBResizeSram(gb, gb->sramSize);

	if (gb->memory.mbcType == GB_MBC3_RTC) {
		GBMBCRTCRead(gb);
	}
}

static void _latchRtc(struct mRTCSource* rtc, uint8_t* rtcRegs, time_t* rtcLastLatch) {
	time_t t;
	if (rtc) {
		if (rtc->sample) {
			rtc->sample(rtc);
		}
		t = rtc->unixTime(rtc);
	} else {
		t = time(0);
	}
	time_t currentLatch = t;
	t -= *rtcLastLatch;
	*rtcLastLatch = currentLatch;

	int64_t diff;
	diff = rtcRegs[0] + t % 60;
	if (diff < 0) {
		diff += 60;
		t -= 60;
	}
	rtcRegs[0] = diff % 60;
	t /= 60;
	t += diff / 60;

	diff = rtcRegs[1] + t % 60;
	if (diff < 0) {
		diff += 60;
		t -= 60;
	}
	rtcRegs[1] = diff % 60;
	t /= 60;
	t += diff / 60;

	diff = rtcRegs[2] + t % 24;
	if (diff < 0) {
		diff += 24;
		t -= 24;
	}
	rtcRegs[2] = diff % 24;
	t /= 24;
	t += diff / 24;

	diff = rtcRegs[3] + ((rtcRegs[4] & 1) << 8) + (t & 0x1FF);
	rtcRegs[3] = diff;
	rtcRegs[4] &= 0xFE;
	rtcRegs[4] |= (diff >> 8) & 1;
	if (diff & 0x200) {
		rtcRegs[4] |= 0x80;
	}
}

void _GBMBC1(struct GB* gb, uint16_t address, uint8_t value) {
	struct GBMemory* memory = &gb->memory;
	int bank = value & 0x1F;
	int stride = 1 << memory->mbcState.mbc1.multicartStride;
	switch (address >> 13) {
	case 0x0:
		switch (value) {
		case 0:
			memory->sramAccess = false;
			break;
		case 0xA:
			memory->sramAccess = true;
			GBMBCSwitchSramBank(gb, memory->sramCurrentBank);
			break;
		default:
			// TODO
			mLOG(GB_MBC, STUB, "MBC1 unknown value %02X", value);
			break;
		}
		break;
	case 0x1:
		if (!bank) {
			++bank;
		}
		bank &= stride - 1;
		GBMBCSwitchBank(gb, bank | (memory->currentBank & (3 * stride)));
		break;
	case 0x2:
		bank &= 3;
		if (memory->mbcState.mbc1.mode) {
			GBMBCSwitchBank0(gb, bank);
			GBMBCSwitchSramBank(gb, bank);
		}
		GBMBCSwitchBank(gb, (bank << memory->mbcState.mbc1.multicartStride) | (memory->currentBank & (stride - 1)));
		break;
	case 0x3:
		memory->mbcState.mbc1.mode = value & 1;
		if (memory->mbcState.mbc1.mode) {
			GBMBCSwitchBank0(gb, memory->currentBank >> memory->mbcState.mbc1.multicartStride);
		} else {
			GBMBCSwitchBank0(gb, 0);
			GBMBCSwitchSramBank(gb, 0);
		}
		break;
	default:
		// TODO
		mLOG(GB_MBC, STUB, "MBC1 unknown address: %04X:%02X", address, value);
		break;
	}
}

void _GBMBC2(struct GB* gb, uint16_t address, uint8_t value) {
	struct GBMemory* memory = &gb->memory;
	int bank = value & 0xF;
	switch (address >> 13) {
	case 0x0:
		switch (value) {
		case 0:
			memory->sramAccess = false;
			break;
		case 0xA:
			memory->sramAccess = true;
			GBMBCSwitchSramBank(gb, memory->sramCurrentBank);
			break;
		default:
			// TODO
			mLOG(GB_MBC, STUB, "MBC1 unknown value %02X", value);
			break;
		}
		break;
	case 0x1:
		if (!bank) {
			++bank;
		}
		GBMBCSwitchBank(gb, bank);
		break;
	default:
		// TODO
		mLOG(GB_MBC, STUB, "MBC2 unknown address: %04X:%02X", address, value);
		break;
	}
}

void _GBMBC3(struct GB* gb, uint16_t address, uint8_t value) {
	struct GBMemory* memory = &gb->memory;
	int bank = value & 0x7F;
	switch (address >> 13) {
	case 0x0:
		switch (value) {
		case 0:
			memory->sramAccess = false;
			break;
		case 0xA:
			memory->sramAccess = true;
			GBMBCSwitchSramBank(gb, memory->sramCurrentBank);
			break;
		default:
			// TODO
			mLOG(GB_MBC, STUB, "MBC3 unknown value %02X", value);
			break;
		}
		break;
	case 0x1:
		if (!bank) {
			++bank;
		}
		GBMBCSwitchBank(gb, bank);
		break;
	case 0x2:
		if (value < 4) {
			GBMBCSwitchSramBank(gb, value);
			memory->rtcAccess = false;
		} else if (value >= 8 && value <= 0xC) {
			memory->activeRtcReg = value - 8;
			memory->rtcAccess = true;
		}
		break;
	case 0x3:
		if (memory->rtcLatched && value == 0) {
			memory->rtcLatched = false;
		} else if (!memory->rtcLatched && value == 1) {
			_latchRtc(gb->memory.rtc, gb->memory.rtcRegs, &gb->memory.rtcLastLatch);
			memory->rtcLatched = true;
		}
		break;
	}
}

void _GBMBC5(struct GB* gb, uint16_t address, uint8_t value) {
	struct GBMemory* memory = &gb->memory;
	int bank;
	switch (address >> 12) {
	case 0x0:
	case 0x1:
		switch (value) {
		case 0:
			memory->sramAccess = false;
			break;
		case 0xA:
			memory->sramAccess = true;
			GBMBCSwitchSramBank(gb, memory->sramCurrentBank);
			break;
		default:
			// TODO
			mLOG(GB_MBC, STUB, "MBC5 unknown value %02X", value);
			break;
		}
		break;
	case 0x2:
		bank = (memory->currentBank & 0x100) | value;
		GBMBCSwitchBank(gb, bank);
		break;
	case 0x3:
		bank = (memory->currentBank & 0xFF) | ((value & 1) << 8);
		GBMBCSwitchBank(gb, bank);
		break;
	case 0x4:
	case 0x5:
		if (memory->mbcType == GB_MBC5_RUMBLE && memory->rumble) {
			memory->rumble->setRumble(memory->rumble, (value >> 3) & 1);
			value &= ~8;
		}
		GBMBCSwitchSramBank(gb, value & 0xF);
		break;
	default:
		// TODO
		mLOG(GB_MBC, STUB, "MBC5 unknown address: %04X:%02X", address, value);
		break;
	}
}

void _GBMBC6(struct GB* gb, uint16_t address, uint8_t value) {
	// TODO
	mLOG(GB_MBC, STUB, "MBC6 unimplemented");
	UNUSED(gb);
	UNUSED(address);
	UNUSED(value);
}

void _GBMBC7(struct GB* gb, uint16_t address, uint8_t value) {
	int bank = value & 0x7F;
	switch (address >> 13) {
	case 0x0:
		switch (value) {
		default:
		case 0:
			gb->memory.mbcState.mbc7.access = 0;
			break;
		case 0xA:
			gb->memory.mbcState.mbc7.access |= 1;
			break;
		}
		break;
	case 0x1:
		GBMBCSwitchBank(gb, bank);
		break;
	case 0x2:
		if (value == 0x40) {
			gb->memory.mbcState.mbc7.access |= 2;
		} else {
			gb->memory.mbcState.mbc7.access &= ~2;
		}
		break;
	case 0x5:
		_GBMBC7Write(&gb->memory, address, value);
	default:
		// TODO
		mLOG(GB_MBC, STUB, "MBC7 unknown address: %04X:%02X", address, value);
		break;
	}
}

uint8_t _GBMBC7Read(struct GBMemory* memory, uint16_t address) {
	struct GBMBC7State* mbc7 = &memory->mbcState.mbc7;
	if (mbc7->access != 3) {
		return 0xFF;
	}
	switch (address & 0xF0) {
	case 0x20:
		if (memory->rotation && memory->rotation->readTiltX) {
			int32_t x = -memory->rotation->readTiltX(memory->rotation);
			x >>= 21;
			x += 0x81D0;
			return x;
		}
		return 0xFF;
	case 0x30:
		if (memory->rotation && memory->rotation->readTiltX) {
			int32_t x = -memory->rotation->readTiltX(memory->rotation);
			x >>= 21;
			x += 0x81D0;
			return x >> 8;
		}
		return 7;
	case 0x40:
		if (memory->rotation && memory->rotation->readTiltY) {
			int32_t y = -memory->rotation->readTiltY(memory->rotation);
			y >>= 21;
			y += 0x81D0;
			return y;
		}
		return 0xFF;
	case 0x50:
		if (memory->rotation && memory->rotation->readTiltY) {
			int32_t y = -memory->rotation->readTiltY(memory->rotation);
			y >>= 21;
			y += 0x81D0;
			return y >> 8;
		}
		return 7;
	case 0x60:
		return 0;
	case 0x80:
		return mbc7->eeprom;
	default:
		return 0xFF;
	}
}

static void _GBMBC7Write(struct GBMemory* memory, uint16_t address, uint8_t value) {
	struct GBMBC7State* mbc7 = &memory->mbcState.mbc7;
	if (mbc7->access != 3) {
		return;
	}
	switch (address & 0xF0) {
	case 0x00:
		mbc7->latch = (value & 0x55) == 0x55;
		return;
	case 0x10:
		mbc7->latch |= (value & 0xAA);
		if (mbc7->latch == 0xAB && memory->rotation && memory->rotation->sample) {
			memory->rotation->sample(memory->rotation);
		}
		mbc7->latch = 0;
		return;
	default:
		mLOG(GB_MBC, STUB, "MBC7 unknown register: %04X:%02X", address, value);
		return;
	case 0x80:
		break;
	}
	GBMBC7Field old = memory->mbcState.mbc7.eeprom;
	value = GBMBC7FieldFillDO(value); // Hi-Z
	if (!GBMBC7FieldIsCS(old) && GBMBC7FieldIsCS(value)) {
		mbc7->state = GBMBC7_STATE_IDLE;
	}
	if (!GBMBC7FieldIsCLK(old) && GBMBC7FieldIsCLK(value)) {
		if (mbc7->state == GBMBC7_STATE_READ_COMMAND || mbc7->state == GBMBC7_STATE_EEPROM_WRITE || mbc7->state == GBMBC7_STATE_EEPROM_WRAL) {
			mbc7->sr <<= 1;
			mbc7->sr |= GBMBC7FieldGetDI(value);
			++mbc7->srBits;
		}
		switch (mbc7->state) {
		case GBMBC7_STATE_IDLE:
			if (GBMBC7FieldIsDI(value)) {
				mbc7->state = GBMBC7_STATE_READ_COMMAND;
				mbc7->srBits = 0;
				mbc7->sr = 0;
			}
			break;
		case GBMBC7_STATE_READ_COMMAND:
			if (mbc7->srBits == 10) {
				mbc7->state = 0x10 | (mbc7->sr >> 6);
				if (mbc7->state & 0xC) {
					mbc7->state &= ~0x3;
				}
				mbc7->srBits = 0;
				mbc7->address = mbc7->sr & 0x7F;
			}
			break;
		case GBMBC7_STATE_DO:
			value = GBMBC7FieldSetDO(value, mbc7->sr >> 15);
			mbc7->sr <<= 1;
			--mbc7->srBits;
			if (!mbc7->srBits) {
				mbc7->state = GBMBC7_STATE_IDLE;
			}
			break;
		default:
			break;
		}
		switch (mbc7->state) {
		case GBMBC7_STATE_EEPROM_EWEN:
			mbc7->writable = true;
			mbc7->state = GBMBC7_STATE_IDLE;
			break;
		case GBMBC7_STATE_EEPROM_EWDS:
			mbc7->writable = false;
			mbc7->state = GBMBC7_STATE_IDLE;
			break;
		case GBMBC7_STATE_EEPROM_WRITE:
			if (mbc7->srBits == 16) {
				if (mbc7->writable) {
					memory->sram[mbc7->address * 2] = mbc7->sr >> 8;
					memory->sram[mbc7->address * 2 + 1] = mbc7->sr;
				}
				mbc7->state = GBMBC7_STATE_IDLE;
			}
			break;
		case GBMBC7_STATE_EEPROM_ERASE:
			if (mbc7->writable) {
				memory->sram[mbc7->address * 2] = 0xFF;
				memory->sram[mbc7->address * 2 + 1] = 0xFF;
			}
			mbc7->state = GBMBC7_STATE_IDLE;
			break;
		case GBMBC7_STATE_EEPROM_READ:
			mbc7->srBits = 16;
			mbc7->sr = memory->sram[mbc7->address * 2] << 8;
			mbc7->sr |= memory->sram[mbc7->address * 2 + 1];
			mbc7->state = GBMBC7_STATE_DO;
			value = GBMBC7FieldClearDO(value);
			break;
		case GBMBC7_STATE_EEPROM_WRAL:
			if (mbc7->srBits == 16) {
				if (mbc7->writable) {
					int i;
					for (i = 0; i < 128; ++i) {
						memory->sram[i * 2] = mbc7->sr >> 8;
						memory->sram[i * 2 + 1] = mbc7->sr;
					}
				}
				mbc7->state = GBMBC7_STATE_IDLE;
			}
			break;
		case GBMBC7_STATE_EEPROM_ERAL:
			if (mbc7->writable) {
				int i;
				for (i = 0; i < 128; ++i) {
					memory->sram[i * 2] = 0xFF;
					memory->sram[i * 2 + 1] = 0xFF;
				}
			}
			mbc7->state = GBMBC7_STATE_IDLE;
			break;
		default:
			break;
		}
	} else if (GBMBC7FieldIsCS(value) && GBMBC7FieldIsCLK(old) && !GBMBC7FieldIsCLK(value)) {
		value = GBMBC7FieldSetDO(value, GBMBC7FieldGetDO(old));
	}
	mbc7->eeprom = value;
}

void _GBHuC3(struct GB* gb, uint16_t address, uint8_t value) {
	struct GBMemory* memory = &gb->memory;
	int bank = value & 0x3F;
	if (address & 0x1FFF) {
		mLOG(GB_MBC, STUB, "HuC-3 unknown value %04X:%02X", address, value);
	}

	switch (address >> 13) {
	case 0x0:
		switch (value) {
		case 0xA:
			memory->sramAccess = true;
			GBMBCSwitchSramBank(gb, memory->sramCurrentBank);
			break;
		default:
			memory->sramAccess = false;
			break;
		}
		break;
	case 0x1:
		GBMBCSwitchBank(gb, bank);
		break;
	case 0x2:
		GBMBCSwitchSramBank(gb, bank);
		break;
	default:
		// TODO
		mLOG(GB_MBC, STUB, "HuC-3 unknown address: %04X:%02X", address, value);
		break;
	}
}

void _GBPocketCam(struct GB* gb, uint16_t address, uint8_t value) {
	struct GBMemory* memory = &gb->memory;
	int bank = value & 0x3F;
	switch (address >> 13) {
	case 0x0:
		switch (value) {
		case 0:
			memory->sramAccess = false;
			break;
		case 0xA:
			memory->sramAccess = true;
			GBMBCSwitchSramBank(gb, memory->sramCurrentBank);
			break;
		default:
			// TODO
			mLOG(GB_MBC, STUB, "Pocket Cam unknown value %02X", value);
			break;
		}
		break;
	case 0x1:
		GBMBCSwitchBank(gb, bank);
		break;
	case 0x2:
		if (value < 0x10) {
			GBMBCSwitchSramBank(gb, value);
			memory->mbcState.pocketCam.registersActive = false;
		} else {
			memory->mbcState.pocketCam.registersActive = true;
		}
		break;
	default:
		mLOG(GB_MBC, STUB, "Pocket Cam unknown address: %04X:%02X", address, value);
		break;
	}
}

uint8_t _GBPocketCamRead(struct GBMemory* memory, uint16_t address) {
	if (memory->mbcState.pocketCam.registersActive) {
		return 0;
	}
	if (!memory->sramAccess) {
		return 0xFF;
	}
	return memory->sramBank[address & (GB_SIZE_EXTERNAL_RAM - 1)];
}

void _GBTAMA5(struct GB* gb, uint16_t address, uint8_t value) {
	struct GBMemory* memory = &gb->memory;
	struct GBTAMA5State* tama5 = &memory->mbcState.tama5;
	switch (address >> 13) {
	case 0x5:
		if (address & 1) {
			tama5->reg = value;
		} else {
			value &= 0xF;
			if (tama5->reg < GBTAMA5_MAX) {
				tama5->registers[tama5->reg] = value;
				uint8_t address = ((tama5->registers[GBTAMA5_CS] << 4) & 0x10) | tama5->registers[GBTAMA5_ADDR_LO];
				uint8_t out = (tama5->registers[GBTAMA5_WRITE_HI] << 4) | tama5->registers[GBTAMA5_WRITE_LO];
				switch (tama5->reg) {
				case GBTAMA5_BANK_LO:
				case GBTAMA5_BANK_HI:
					GBMBCSwitchBank(gb, tama5->registers[GBTAMA5_BANK_LO] | (tama5->registers[GBTAMA5_BANK_HI] << 4));
					break;
				case GBTAMA5_WRITE_LO:
				case GBTAMA5_WRITE_HI:
				case GBTAMA5_CS:
					break;
				case GBTAMA5_ADDR_LO:
					switch (tama5->registers[GBTAMA5_CS] >> 1) {
					case 0x0: // RAM write
						memory->sram[address] = out;
						break;
					case 0x1: // RAM read
						break;
					default:
						mLOG(GB_MBC, STUB, "TAMA5 unknown address: %X-%02X:%02X", tama5->registers[GBTAMA5_CS] >> 1, address, out);
					}
					break;
				default:
					mLOG(GB_MBC, STUB, "TAMA5 unknown write: %02X:%X", tama5->reg, value);
					break;
				}
			} else {
				mLOG(GB_MBC, STUB, "TAMA5 unknown write: %02X", tama5->reg);
			}
		}
		break;
	default:
		mLOG(GB_MBC, STUB, "TAMA5 unknown address: %04X:%02X", address, value);
	}
}

uint8_t _GBTAMA5Read(struct GBMemory* memory, uint16_t address) {
	struct GBTAMA5State* tama5 = &memory->mbcState.tama5;
	if ((address & 0x1FFF) > 1) {
		mLOG(GB_MBC, STUB, "TAMA5 unknown address: %04X", address);
	}
	if (address & 1) {
		return 0xFF;
	} else {
		uint8_t value = 0xF0;
		uint8_t address = ((tama5->registers[GBTAMA5_CS] << 4) & 0x10) | tama5->registers[GBTAMA5_ADDR_LO];
		switch (tama5->reg) {
		case GBTAMA5_ACTIVE:
			return 0xF1;
		case GBTAMA5_READ_LO:
		case GBTAMA5_READ_HI:
			switch (tama5->registers[GBTAMA5_CS] >> 1) {
			case 1:
				value = memory->sram[address];
				break;
			default:
				mLOG(GB_MBC, STUB, "TAMA5 unknown read: %02X", tama5->reg);
				break;
			}
			if (tama5->reg == GBTAMA5_READ_HI) {
				value >>= 4;
			}
			value |= 0xF0;
			return value;
		default:
			mLOG(GB_MBC, STUB, "TAMA5 unknown read: %02X", tama5->reg);
			return 0xF1;
		}
	}
}

void GBMBCRTCRead(struct GB* gb) {
	struct GBMBCRTCSaveBuffer rtcBuffer;
	struct VFile* vf = gb->sramVf;
	if (!vf) {
		return;
	}
	ssize_t end = vf->seek(vf, -sizeof(rtcBuffer), SEEK_END);
	switch (end & 0x1FFF) {
	case 0:
		break;
	case 0x1FFC:
		vf->seek(vf, -sizeof(rtcBuffer) - 4, SEEK_END);
		break;
	default:
		return;
	}
	vf->read(vf, &rtcBuffer, sizeof(rtcBuffer));

	LOAD_32LE(gb->memory.rtcRegs[0], 0, &rtcBuffer.latchedSec);
	LOAD_32LE(gb->memory.rtcRegs[1], 0, &rtcBuffer.latchedMin);
	LOAD_32LE(gb->memory.rtcRegs[2], 0, &rtcBuffer.latchedHour);
	LOAD_32LE(gb->memory.rtcRegs[3], 0, &rtcBuffer.latchedDays);
	LOAD_32LE(gb->memory.rtcRegs[4], 0, &rtcBuffer.latchedDaysHi);
	LOAD_64LE(gb->memory.rtcLastLatch, 0, &rtcBuffer.unixTime);
}

void GBMBCRTCWrite(struct GB* gb) {
	struct VFile* vf = gb->sramVf;
	if (!vf) {
		return;
	}

	uint8_t rtcRegs[5];
	memcpy(rtcRegs, gb->memory.rtcRegs, sizeof(rtcRegs));
	time_t rtcLastLatch = gb->memory.rtcLastLatch;
	_latchRtc(gb->memory.rtc, rtcRegs, &rtcLastLatch);

	struct GBMBCRTCSaveBuffer rtcBuffer;
	STORE_32LE(rtcRegs[0], 0, &rtcBuffer.sec);
	STORE_32LE(rtcRegs[1], 0, &rtcBuffer.min);
	STORE_32LE(rtcRegs[2], 0, &rtcBuffer.hour);
	STORE_32LE(rtcRegs[3], 0, &rtcBuffer.days);
	STORE_32LE(rtcRegs[4], 0, &rtcBuffer.daysHi);
	STORE_32LE(gb->memory.rtcRegs[0], 0, &rtcBuffer.latchedSec);
	STORE_32LE(gb->memory.rtcRegs[1], 0, &rtcBuffer.latchedMin);
	STORE_32LE(gb->memory.rtcRegs[2], 0, &rtcBuffer.latchedHour);
	STORE_32LE(gb->memory.rtcRegs[3], 0, &rtcBuffer.latchedDays);
	STORE_32LE(gb->memory.rtcRegs[4], 0, &rtcBuffer.latchedDaysHi);
	STORE_64LE(rtcLastLatch, 0, &rtcBuffer.unixTime);

	if (vf->size(vf) == gb->sramSize) {
		// Writing past the end of the file can invalidate the file mapping
		vf->unmap(vf, gb->memory.sram, gb->sramSize);
		gb->memory.sram = NULL;
	}
	vf->seek(vf, gb->sramSize, SEEK_SET);
	vf->write(vf, &rtcBuffer, sizeof(rtcBuffer));
	if (!gb->memory.sram) {
		gb->memory.sram = vf->map(vf, gb->sramSize, MAP_WRITE);
		GBMBCSwitchSramBank(gb, gb->memory.sramCurrentBank);
	}
}
