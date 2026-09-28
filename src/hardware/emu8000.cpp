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

/* EMU8000 (Sound Blaster AWE32).
 *
 * The chip itself is emu8k/snd_emu8k.c: the EMU8000 of 86Box with the
 * corrections measured on a real AWE32 by the AWE32Emu project (filter,
 * envelopes, interpolation, reverb/chorus/EQ, output level). This file
 * connects it to DOSBox-X: the I/O ports, the mixer channel and the timing.
 *
 * Timing. In 86Box the chip renders into a block of WTBUFLEN (980) frames at
 * 44100 Hz; every port write first brings the chip up to the current frame
 * (emu8k_update renders up to wavetable_pos_global). Here the mixer channel
 * callback renders, and a port access first calls FillUp(), which runs the
 * mixer - and so the chip - up to the current emulated time. The sample
 * counter (WC) that drivers use for delays therefore follows the emulated
 * time, and every register write lands at its own frame, as on the card.
 * The block is reset every 980 frames, as in 86Box.
 *
 * Debugging: with the environment variable EMU8K_TRACE=<file> every port
 * write is recorded as "<frame> <port hex> <value hex>" (a byte write as the
 * word write the chip turns it into), frames counted at 44100 Hz from the
 * chip reset - the format of AWE32Emu (--replay) and of the instrumented
 * 86Box, so a run can be replayed and compared outside DOSBox-X.
 */

#include "dosbox.h"
#include "inout.h"
#include "logging.h"
#include "mixer.h"
#include "pic.h"
#include "emu8000.h"
#include "awe32_rom.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#if defined(WIN32)
#include <windows.h>
#endif

extern "C" {
#include "emu8k/include/86box/sound.h"
#include "emu8k/include/86box/snd_emu8k.h"

/* Defined in snd_emu8k.c, not declared in its header. */
uint16_t emu8k_inw(uint16_t addr, void *priv);
uint8_t  emu8k_inb(uint16_t addr, void *priv);
void     emu8k_outw(uint16_t addr, uint16_t val, void *priv);
void     emu8k_outb(uint16_t addr, uint8_t val, void *priv);
}

/* --- services snd_emu8k.c expects from 86Box --------------------------------- */

namespace {
	std::string rom_file;	/* the ROM rom_fopen() hands to the chip */
}

extern "C" {

int music_pos_global = 0;
int wavetable_pos_global = 0;

void pclog_ex(const char *fmt, va_list ap) {
	(void)fmt; (void)ap;
}

void pclog(const char *fmt, ...) {
	(void)fmt;
}

void fatal(const char *fmt, ...) {
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	E_Exit("EMU8000: %s", buf);
}

FILE *rom_fopen(const char *fn, char *mode) {
	(void)fn;
	return rom_file.empty() ? NULL : fopen(rom_file.c_str(), mode);
}

/* DOSBox-X installs the port handlers itself (see EMU8000_Device). */
void io_sethandler(uint16_t, uint16_t,
		uint8_t (*)(uint16_t, void *), uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
		void (*)(uint16_t, uint8_t, void *), void (*)(uint16_t, uint16_t, void *), void (*)(uint16_t, uint32_t, void *),
		void *) {
}

void io_removehandler(uint16_t, uint16_t,
		uint8_t (*)(uint16_t, void *), uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
		void (*)(uint16_t, uint8_t, void *), void (*)(uint16_t, uint16_t, void *), void (*)(uint16_t, uint32_t, void *),
		void *) {
}

} // extern "C"

namespace {

const unsigned int EMU_PORT_GROUPS = 3;
const unsigned int EMU_PORT_GROUP_STRIDE = 0x400;
const Bitu EMU_RATE = 44100;
const long ROM_BYTES = 1048576;

/* A zero-filled ROM for running without the real one: the chip works, the
 * drivers find it, only the ROM sounds are silent. */
std::string MakeEmptyRom(void) {
	std::string path;
#if defined(WIN32)
	char dir[MAX_PATH];
	const DWORD n = GetTempPathA(MAX_PATH, dir);
	if (n == 0 || n >= MAX_PATH) return std::string();
	path = std::string(dir) + "dosbox-x-awe32-empty.rom";
#else
	const char *tmp = getenv("TMPDIR");
	path = std::string(tmp != NULL && *tmp ? tmp : "/tmp") + "/dosbox-x-awe32-empty.rom";
#endif
	FILE *f = fopen(path.c_str(), "wb");
	if (f == NULL) return std::string();
	static const char zero[4096] = { 0 };
	for (long done = 0; done < ROM_BYTES; done += (long)sizeof(zero)) fwrite(zero, 1, sizeof(zero), f);
	fclose(f);
	return path;
}

class EMU8000_Device {
public:
	EMU8000_Device(unsigned int sb_base, const std::string &rom, int ram_kb);
	~EMU8000_Device();

	Bitu Read(Bitu port, Bitu iolen);
	void Write(Bitu port, Bitu val, Bitu iolen);
	void Mix(Bitu len);

private:
	/* Brings the chip up to the current emulated time. The mixer is run only
	 * when at least one sample has passed since the last time, so a driver
	 * uploading samples through SMLD does not run it on every write. */
	void Sync(void) {
		const pic_tickindex_t now = PIC_FullIndex();
		if (now - last_sync >= sample_ms || now < last_sync) {
			last_sync = now;
			if (chan != NULL) chan->FillUp();
		}
	}

	void Trace(Bitu port, Bitu val) {
		if (trace != NULL)
			fprintf(trace, "%llu %03X %04X\n", (unsigned long long)(frames_done + (uint64_t)emu->pos),
				(unsigned int)port, (unsigned int)val);
	}

	emu8k_t *emu;
	uint64_t frames_done;			/* frames of the finished blocks */
	FILE *trace;
	Bitu base;					/* SB base + 400h */
	pic_tickindex_t last_sync;
	const pic_tickindex_t sample_ms;
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

EMU8000_Device::EMU8000_Device(unsigned int sb_base, const std::string &rom, int ram_kb)
	: emu(NULL), frames_done(0), trace(NULL), base((Bitu)sb_base + 0x400u), last_sync(0), sample_ms(1000.0 / EMU_RATE), chan(NULL) {
	std::string empty_rom;
	if (rom.empty()) {
		empty_rom = MakeEmptyRom();
		if (empty_rom.empty()) E_Exit("EMU8000: cannot create a temporary empty ROM");
		rom_file = empty_rom;
		LOG_MSG("AWE32: no wave ROM - the EMU8000 runs with an empty ROM, its General MIDI sounds are silent");
	}
	else {
		rom_file = rom;
	}

	emu = (emu8k_t *)calloc(1, sizeof(emu8k_t));
	if (emu == NULL) E_Exit("EMU8000: out of memory");
	/* The chip is built for one instance; its I/O address only matters to
	 * 86Box's handler registration, which is a no-op here. */
	emu8k_init(emu, (uint16_t)base, ram_kb);
	wavetable_pos_global = emu->pos;
	if (!empty_rom.empty()) remove(empty_rom.c_str());

	for (unsigned int g = 0; g < EMU_PORT_GROUPS; g++) {
		ReadHandler[g].Install(base + g * EMU_PORT_GROUP_STRIDE, emu8000_read, IO_MA, 4);
		WriteHandler[g].Install(base + g * EMU_PORT_GROUP_STRIDE, emu8000_write, IO_MA, 4);
	}

	/* The channel stays enabled: the chip clock (WC) advances only while the
	 * mixer runs the callback, and drivers wait on it. */
	chan = MixerChan.Install(emu8000_callback, EMU_RATE, EMU8000_MIXER_CHANNEL);
	if (chan != NULL) chan->Enable(true);
	last_sync = PIC_FullIndex();

	const char *trace_path = getenv("EMU8K_TRACE");
	if (trace_path != NULL && *trace_path) {
		trace = fopen(trace_path, "w");
		if (trace != NULL) fprintf(trace, "# EMU8000 port write trace (DOSBox-X), 44100 Hz timebase\n# <frame> <port hex> <value hex>\n");
	}

	LOG_MSG("AWE32: EMU8000 at %03Xh/%03Xh/%03Xh, %d KB sample RAM, ROM %s",
		(unsigned int)base, (unsigned int)(base + EMU_PORT_GROUP_STRIDE), (unsigned int)(base + 2 * EMU_PORT_GROUP_STRIDE),
		ram_kb, rom.empty() ? "(none)" : rom.c_str());
}

EMU8000_Device::~EMU8000_Device() {
	/* the I/O handler and mixer objects clean up after themselves */
	if (trace != NULL) fclose(trace);
	if (emu != NULL) {
		emu8k_close(emu);
		free(emu->empty);
		free(emu);
		emu = NULL;
	}
}

Bitu EMU8000_Device::Read(Bitu port, Bitu iolen) {
	Sync();
	if (iolen >= 4) {	/* the ISA bus splits a dword into two words, lower address first */
		const Bitu lo = emu8k_inw((uint16_t)port, emu);
		const Bitu hi = emu8k_inw((uint16_t)(port + 2), emu);
		return lo | (hi << 16);
	}
	if (iolen == 2) return emu8k_inw((uint16_t)port, emu);
	return emu8k_inb((uint16_t)port, emu);
}

void EMU8000_Device::Write(Bitu port, Bitu val, Bitu iolen) {
	Sync();
	if (iolen >= 4) {
		Trace(port, val & 0xFFFF);
		emu8k_outw((uint16_t)port, (uint16_t)(val & 0xFFFF), emu);
		Trace(port + 2, (val >> 16) & 0xFFFF);
		emu8k_outw((uint16_t)(port + 2), (uint16_t)((val >> 16) & 0xFFFF), emu);
	}
	else if (iolen == 2) {
		Trace(port, val & 0xFFFF);
		emu8k_outw((uint16_t)port, (uint16_t)val, emu);
	}
	else {
		/* recorded as the word write emu8k_outb() turns it into */
		Trace(port & ~(Bitu)1, (port & 1) ? (val & 0xFF) << 8 : (val & 0xFF));
		emu8k_outb((uint16_t)port, (uint8_t)val, emu);
	}
}

void EMU8000_Device::Mix(Bitu len) {
	/* wavetable_pos_global always equals emu->pos outside this function, so
	 * the emu8k_update() that every emu8k_outw() runs renders nothing. */
	while (len > 0) {
		const int start = emu->pos;
		int n = WTBUFLEN - start;
		if ((Bitu)n > len) n = (int)len;

		wavetable_pos_global = start + n;
		emu8k_update(emu);
		chan->AddSamples_s32((Bitu)n, &emu->buffer[start * 2]);
		len -= (Bitu)n;

		if (emu->pos >= WTBUFLEN) {
			emu8k_reset_buffer(emu);
			frames_done += WTBUFLEN;
		}
		wavetable_pos_global = emu->pos;
	}
}

} // anonymous namespace

void EMU8000_Init(unsigned int sb_base, const std::string &rom_path, int ram_kb) {
	EMU8000_ShutDown();
	const std::string rom = AWE32ROM_Locate(rom_path);
	emu8000 = new EMU8000_Device(sb_base, rom, ram_kb);
}

void EMU8000_ShutDown(void) {
	delete emu8000;
	emu8000 = NULL;
}
