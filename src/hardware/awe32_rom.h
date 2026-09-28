/*
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

#ifndef DOSBOX_AWE32_ROM_H
#define DOSBOX_AWE32_ROM_H

#include <string>

/* Wave ROM of the Sound Blaster AWE32 (EMU8000), 1 MB.
 *
 * The ROM is Creative's and is not part of DOSBox-X. It is looked up in
 * this order:
 *   1. the path from the option awe32rom= ([sblaster])
 *   2. AWE32ROM/awe32.raw next to the DOSBox-X executable
 *   3. AWE32ROM/awe32.raw in the DOSBox-X configuration directory
 *   4. AWE32ROM/awe32.raw in the current directory
 *
 * When none is found, the user is asked (Yes/No) whether to download it,
 * with the file name, the source URL and the target path in the question.
 * It is saved to AWE32ROM/ next to the executable, or in the configuration
 * directory when that one is not writable. Nothing is downloaded without a
 * "Yes"; the question comes once per session. A downloaded file is kept only
 * when its size and SHA-256 match the known ROM image. */

/* The known image (1048576 bytes). */
extern const char *AWE32ROM_URL;

/* Registers the user-facing messages (MSG_Add), so that language files can
 * translate them. */
void AWE32ROM_AddMessages(void);

/* Returns the path of a usable ROM, or an empty string when there is none
 * (not found and not downloaded). */
std::string AWE32ROM_Locate(const std::string &configured);

#endif
