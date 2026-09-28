# EMU8000 chip (Sound Blaster AWE32)

`snd_emu8k.c` is the EMU8000 emulation from [86Box](https://github.com/86Box/86Box)
(`src/sound/snd_emu8k.c`) with the corrections measured on a real Sound
Blaster AWE32 by the [AWE32Emu](https://github.com/turican0/AWE32Emu) project:
filter (Chamberlin structure and cutoff mapping), envelopes, interpolation,
reverb, chorus, the equaliser, the output level and fixes of upstream bugs.
The changes are marked `AWE32Emu:` in the code.

`include/86box/` holds `snd_emu8k.h` and small stand-ins for the 86Box headers
the file includes, enough to build the chip outside 86Box. The services it
expects from 86Box (`rom_fopen`, `io_sethandler`, `pclog`, `fatal`,
`wavetable_pos_global`) are provided by `../emu8000.cpp`, which also connects
the chip to the DOSBox-X ports, mixer and timing.

**Keep it identical.** This file is a byte-identical copy of
`AWE32Emu/src/86box/snd_emu8k.c`. Change the chip there (and in the 86Box
tree used for the measurements) and copy it over; do not edit it here.

**Wave ROM.** The chip needs the 1 MB ROM image `awe32.raw`, which is
Creative's and not part of DOSBox-X; see `../awe32_rom.h` and the options
`awe32rom` and `awe32ram` in the `[sblaster]` section; when the ROM is
missing, DOSBox-X asks (Yes/No) whether to download it.

**License:** 86Box, and so this file, is licensed under the GNU General Public
License, version 2 or later.
