/*
 *  Copyright (C) 2002-2021  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/* EMU8000 (Sound Blaster AWE32) - skeleton.
 *
 * What this does so far:
 *   - claims the three EMU8000 I/O port groups (base+400h/800h/C00h)
 *   - implements the pointer register and a plain read-back register file,
 *     so a driver probing the chip sees the values it wrote
 *   - installs a (silent) 44100 Hz stereo mixer channel called "AWE32"
 *
 * TODO (hook the real emulation in here):
 *   - decode registers (CPF, PTRX, CVCF, VTFT, PSST, CSL, CCCA, ..., HWCF1-3, INIT1-4)
 *   - sample DRAM / ROM access (SMALR/SMARR/SMALW/SMARW/SMLD/SMRD)
 *   - voice synthesis in Mix(), effects (reverb/chorus)
 */

#include "dosbox.h"
#include "inout.h"
#include "logging.h"
#include "mixer.h"
#include "emu8000.h"

#include <string.h>

namespace {

/* 16-bit "slots" in the port groups, counted from the EMU8000 base (SB base + 400h) */
enum {
	SLOT_DATA0L = 0,	/* base+000h */
	SLOT_DATA0H,		/* base+002h */
	SLOT_DATA1,		/* base+400h */
	SLOT_DATA2,		/* base+402h */
	SLOT_DATA3,		/* base+800h */
	SLOT_PTR,		/* base+802h */
	SLOT_MAX
};

const unsigned int EMU_PORT_GROUPS = 3;
const unsigned int EMU_PORT_GROUP_STRIDE = 0x400;
const unsigned int EMU_PTR_ENTRIES = 0x1000;	/* pointer = (register << 5) | voice */
const Bitu EMU_MIXER_RATE = 44100;

class EMU8000_Device {
public:
	EMU8000_Device(unsigned int sb_base);
	~EMU8000_Device();

	Bitu Read(Bitu port, Bitu iolen);
	void Write(Bitu port, Bitu val, Bitu iolen);
	void Mix(Bitu len);

private:
	uint16_t ReadWord(unsigned int slot) const {
		if (slot == SLOT_PTR) return ptr;
		return regs[slot][ptr % EMU_PTR_ENTRIES];
	}
	void WriteWord(unsigned int slot, uint16_t val) {
		if (slot == SLOT_PTR) ptr = val;
		else regs[slot][ptr % EMU_PTR_ENTRIES] = val;
	}
	unsigned int SlotOf(Bitu port) const {
		const unsigned int off = (unsigned int)(port - base);
		return ((off / EMU_PORT_GROUP_STRIDE) % EMU_PORT_GROUPS) * 2u + ((off >> 1) & 1u);
	}

	Bitu base;					/* SB base + 400h */
	uint16_t ptr;					/* last value written to the pointer register */
	uint16_t regs[SLOT_MAX - 1][EMU_PTR_ENTRIES];	/* read-back register file (all data ports) */
	IO_ReadHandleObject ReadHandler[EMU_PORT_GROUPS];
	IO_WriteHandleObject WriteHandler[EMU_PORT_GROUPS];
	MixerObject MixerChan;
	MixerChannel *chan;
};

EMU8000_Device *emu8000 = NULL;

Bitu emu8000_read(Bitu port, Bitu iolen) {
	return emu8000 != NULL ? emu8000->Read(port, iolen) : ~(Bitu)0;
}

void emu8000_write(Bitu port, Bitu val, Bitu iolen) {
	if (emu8000 != NULL) emu8000->Write(port, val, iolen);
}

void emu8000_callback(Bitu len) {
	if (emu8000 != NULL) emu8000->Mix(len);
}

EMU8000_Device::EMU8000_Device(unsigned int sb_base) : base((Bitu)sb_base + 0x400u), ptr(0), chan(NULL) {
	memset(regs, 0, sizeof(regs));

	for (unsigned int g = 0; g < EMU_PORT_GROUPS; g++) {
		ReadHandler[g].Install(base + g * EMU_PORT_GROUP_STRIDE, emu8000_read, IO_MA, 4);
		WriteHandler[g].Install(base + g * EMU_PORT_GROUP_STRIDE, emu8000_write, IO_MA, 4);
	}

	chan = MixerChan.Install(emu8000_callback, EMU_MIXER_RATE, "AWE32");
	if (chan != NULL) chan->Enable(true);

	LOG(LOG_SB, LOG_NORMAL)("EMU8000 (AWE32): I/O ports %03Xh, %03Xh, %03Xh (no synthesis yet)",
		(unsigned int)base, (unsigned int)(base + EMU_PORT_GROUP_STRIDE), (unsigned int)(base + 2 * EMU_PORT_GROUP_STRIDE));
}

EMU8000_Device::~EMU8000_Device() {
	/* the I/O handler and mixer objects clean up after themselves */
}

Bitu EMU8000_Device::Read(Bitu port, Bitu iolen) {
	unsigned int slot = SlotOf(port);

	if (iolen >= 4) {
		slot &= ~1u;	/* 32-bit access covers a pair of 16-bit slots */
		return (Bitu)ReadWord(slot) | ((Bitu)ReadWord(slot + 1) << 16);
	}

	const uint16_t w = ReadWord(slot);
	if (iolen == 2) return w;
	return (port & 1) ? (Bitu)(w >> 8) : (Bitu)(w & 0xFF);
}

void EMU8000_Device::Write(Bitu port, Bitu val, Bitu iolen) {
	unsigned int slot = SlotOf(port);

	if (iolen >= 4) {
		slot &= ~1u;	/* 32-bit access covers a pair of 16-bit slots */
		WriteWord(slot, (uint16_t)(val & 0xFFFF));
		WriteWord(slot + 1, (uint16_t)((val >> 16) & 0xFFFF));
	}
	else if (iolen == 2) {
		WriteWord(slot, (uint16_t)(val & 0xFFFF));
	}
	else {
		uint16_t w = ReadWord(slot);
		if (port & 1) w = (uint16_t)((w & 0x00FF) | ((val & 0xFF) << 8));
		else w = (uint16_t)((w & 0xFF00) | (val & 0xFF));
		WriteWord(slot, w);
	}
}

void EMU8000_Device::Mix(Bitu len) {
	/* TODO: render the 32 voices here. For now the channel is silent. */
	static const int16_t silence[256 * 2] = { 0 };

	while (len > 0) {
		const Bitu n = len > 256 ? 256 : len;
		chan->AddSamples_s16(n, silence);
		len -= n;
	}
}

} // anonymous namespace

void EMU8000_Init(unsigned int sb_base) {
	EMU8000_ShutDown();
	emu8000 = new EMU8000_Device(sb_base);
}

void EMU8000_ShutDown(void) {
	delete emu8000;
	emu8000 = NULL;
}
