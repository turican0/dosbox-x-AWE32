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

/* Creative EMU8000 wavetable synthesizer, the synth chip of the Sound Blaster AWE32
 * (sbtype=sbawe).
 *
 * The chip is programmed by the DOS driver through three groups of four I/O ports
 * located at Sound Blaster base + 400h / 800h / C00h (620h, A20h, E20h for base 220h):
 *
 *   620h DATA0 (low word)    622h DATA0 (high word, for 32-bit registers)
 *   A20h DATA1               A22h DATA2
 *   E20h DATA3               E22h POINTER  ((register << 5) | voice)
 */

/* sb_base: I/O base of the Sound Blaster card the EMU8000 is attached to */
void EMU8000_Init(unsigned int sb_base);
void EMU8000_ShutDown(void);

#endif
