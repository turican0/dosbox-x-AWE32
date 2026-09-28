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

#ifndef DOSBOX_EMU8000_H
#define DOSBOX_EMU8000_H

#include <string>

/* Creative EMU8000 wavetable synthesizer, the synth chip of the Sound Blaster AWE32
 * (sbtype=sbawe).
 *
 * The chip is programmed by the DOS driver through three groups of four I/O ports
 * located at Sound Blaster base + 400h / 800h / C00h (620h, A20h, E20h for base 220h):
 *
 *   620h DATA0 (low word)    622h DATA0 (high word, for 32-bit registers)
 *   A20h DATA1               A22h DATA2
 *   E20h DATA3               E22h POINTER  ((register << 5) | voice)
 *
 * The emulation is snd_emu8k.c from 86Box with the corrections measured on a real
 * AWE32 by the AWE32Emu project (src/hardware/emu8k/README.md). Its output is the
 * mixer channel "AWE32", which follows the MIDI volume of the SB16 mixer, as on the
 * card.
 */

/* Name of the mixer channel. */
#define EMU8000_MIXER_CHANNEL "AWE32"

/* sb_base: I/O base of the Sound Blaster card the EMU8000 is attached to
 * rom_path: awe32rom= setting (empty = search, see awe32_rom.h)
 * ram_kb: sample DRAM on the card in KB (awe32ram=) */
void EMU8000_Init(unsigned int sb_base, const std::string &rom_path, int ram_kb);
void EMU8000_ShutDown(void);

#endif
