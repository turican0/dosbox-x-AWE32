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

/* Locating and, on request, downloading the Sound Blaster AWE32 wave ROM.
 * See awe32_rom.h for the search order. */

#include "dosbox.h"
#include "logging.h"
#include "control.h"
#include "cross.h"
#include "awe32_rom.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include <sys/stat.h>

#if defined(WIN32)
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#if defined(MACOSX)
#include <mach-o/dyld.h>
#endif
#endif

bool systemmessagebox(char const * aTitle, char const * aMessage, char const * aDialogType, char const * aIconType, int aDefaultButton);

/* The ROM image published with libretro-pcem; the same 1 MB dump that 86Box
 * and AWE32Emu use. */
const char *AWE32ROM_URL = "https://raw.githubusercontent.com/libretro/libretro-pcem/master/awe32.raw";

namespace {

const char *ROM_DIR = "AWE32ROM";
const char *ROM_FILE = "awe32.raw";
const long ROM_SIZE = 1048576;
/* An AWEDUMP image lacks the first word; snd_emu8k.c accepts both. */
const long ROM_SIZE_MIN = 1048574;
const char *ROM_SHA256 = "4e143b94f758734f594ded78f4e5115635975c16fa37fabeae0baa4938ca710e";

/* A user who declined the download is not asked again in this session
 * (the Sound Blaster is re-initialised when its settings change). */
bool asked_this_session = false;

/* --- SHA-256 (FIPS 180-4) ------------------------------------------------ */

class Sha256 {
public:
	Sha256() {
		static const uint32_t init[8] = {
			0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
		memcpy(h, init, sizeof(h));
	}
	void Update(const uint8_t *p, size_t n) {
		while (n > 0) {
			const size_t take = (64 - fill) < n ? (64 - fill) : n;
			memcpy(block + fill, p, take);
			fill += take; p += take; n -= take; total += take;
			if (fill == 64) { Compress(); fill = 0; }
		}
	}
	std::string HexDigest() {
		const uint64_t bits = total * 8;
		const uint8_t pad = 0x80;
		Update(&pad, 1);
		const uint8_t zero = 0;
		while (fill != 56) Update(&zero, 1);
		uint8_t len[8];
		for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
		Update(len, 8);
		static const char hex[] = "0123456789abcdef";
		std::string out;
		for (int i = 0; i < 8; i++)
			for (int s = 28; s >= 0; s -= 4) out += hex[(h[i] >> s) & 0xF];
		return out;
	}
private:
	static uint32_t Ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
	void Compress() {
		static const uint32_t k[64] = {
			0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
			0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
			0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
			0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
			0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
			0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
			0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
			0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
		uint32_t w[64];
		for (int i = 0; i < 16; i++)
			w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) | ((uint32_t)block[i*4+2] << 8) | block[i*4+3];
		for (int i = 16; i < 64; i++) {
			const uint32_t s0 = Ror(w[i-15], 7) ^ Ror(w[i-15], 18) ^ (w[i-15] >> 3);
			const uint32_t s1 = Ror(w[i-2], 17) ^ Ror(w[i-2], 19) ^ (w[i-2] >> 10);
			w[i] = w[i-16] + s0 + w[i-7] + s1;
		}
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
		for (int i = 0; i < 64; i++) {
			const uint32_t t1 = hh + (Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
			const uint32_t t2 = (Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
			hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
	}
	uint32_t h[8];
	uint8_t block[64];
	size_t fill = 0;
	uint64_t total = 0;
};

/* --- files ----------------------------------------------------------------- */

long FileSize(const std::string &path) {
	FILE *f = fopen(path.c_str(), "rb");
	if (f == NULL) return -1;
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	fclose(f);
	return n;
}

bool IsUsableRom(const std::string &path) {
	const long n = FileSize(path);
	return n >= ROM_SIZE_MIN;
}

std::string FileSha256(const std::string &path) {
	FILE *f = fopen(path.c_str(), "rb");
	if (f == NULL) return std::string();
	Sha256 sha;
	std::vector<uint8_t> buf(65536);
	size_t n;
	while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) sha.Update(buf.data(), n);
	fclose(f);
	return sha.HexDigest();
}

std::string Join(const std::string &dir, const std::string &name) {
	if (dir.empty()) return name;
	const char last = dir[dir.size() - 1];
	if (last == '/' || last == '\\') return dir + name;
	return dir + CROSS_FILESPLIT + name;
}

std::string ExecutableDir(void) {
	char buf[4096];
#if defined(WIN32)
	const DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));
	if (n == 0 || n >= sizeof(buf)) return std::string();
	std::string p(buf, n);
#elif defined(MACOSX)
	uint32_t size = sizeof(buf);
	if (_NSGetExecutablePath(buf, &size) != 0) return std::string();
	std::string p(buf);
#else
	const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n <= 0) return std::string();
	buf[n] = 0;
	std::string p(buf);
#endif
	const size_t cut = p.find_last_of("/\\");
	return cut == std::string::npos ? std::string() : p.substr(0, cut);
}

bool MakeDir(const std::string &dir) {
	struct stat st;
	if (stat(dir.c_str(), &st) == 0) return (st.st_mode & S_IFDIR) != 0;
#if defined(WIN32)
	return _mkdir(dir.c_str()) == 0;
#else
	return mkdir(dir.c_str(), 0755) == 0;
#endif
}

/* A directory we can create a file in. */
bool Writable(const std::string &dir) {
	if (!MakeDir(dir)) return false;
	const std::string probe = Join(dir, ".awe32rom-write-test");
	FILE *f = fopen(probe.c_str(), "wb");
	if (f == NULL) return false;
	fclose(f);
	remove(probe.c_str());
	return true;
}

/* --- download -------------------------------------------------------------- */

#if defined(WIN32)
/* urlmon is loaded at run time, so neither build system needs a new library. */
bool DownloadFile(const char *url, const std::string &dest, std::string &err) {
	typedef HRESULT (WINAPI *URLDownloadToFileA_t)(void *, const char *, const char *, DWORD, void *);
	HMODULE urlmon = LoadLibraryA("urlmon.dll");
	if (urlmon == NULL) { err = "urlmon.dll is not available"; return false; }
	URLDownloadToFileA_t fn = (URLDownloadToFileA_t)(void *)GetProcAddress(urlmon, "URLDownloadToFileA");
	if (fn == NULL) { FreeLibrary(urlmon); err = "URLDownloadToFileA is not available"; return false; }
	const HRESULT hr = fn(NULL, url, dest.c_str(), 0, NULL);
	FreeLibrary(urlmon);
	if (FAILED(hr)) {
		char msg[64];
		snprintf(msg, sizeof(msg), "download failed (HRESULT 0x%08lX)", (unsigned long)hr);
		err = msg;
		return false;
	}
	return true;
}
#else
/* Runs a program without a shell (no quoting of the path is needed) and
 * returns its exit code, or 127 when it could not be started. */
int RunProgram(const char *const argv[]) {
	const pid_t pid = fork();
	if (pid < 0) return 127;
	if (pid == 0) {
		execvp(argv[0], (char *const *)argv);
		_exit(127);
	}
	int status = 0;
	if (waitpid(pid, &status, 0) < 0) return 127;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 127;
}

bool DownloadFile(const char *url, const std::string &dest, std::string &err) {
	const char *curl[] = { "curl", "-fsSL", "--max-time", "300", "-o", dest.c_str(), url, NULL };
	int rc = RunProgram(curl);
	if (rc == 0) return true;
	if (rc == 127) {
		const char *wget[] = { "wget", "-q", "-T", "300", "-O", dest.c_str(), url, NULL };
		rc = RunProgram(wget);
		if (rc == 0) return true;
		if (rc == 127) { err = "neither curl nor wget is installed"; return false; }
	}
	char msg[64];
	snprintf(msg, sizeof(msg), "the download tool exited with code %d", rc);
	err = msg;
	return false;
}
#endif

/* Downloads into dir/awe32.raw through a temporary file; the result is kept
 * only when it is the known image. */
bool DownloadRom(const std::string &dir, std::string &path, std::string &err) {
	if (!MakeDir(dir)) { err = "cannot create " + dir; return false; }
	const std::string part = Join(dir, std::string(ROM_FILE) + ".part");
	const std::string dest = Join(dir, ROM_FILE);
	remove(part.c_str());

	LOG_MSG("AWE32: downloading the wave ROM from %s to %s", AWE32ROM_URL, dest.c_str());
	if (!DownloadFile(AWE32ROM_URL, part, err)) { remove(part.c_str()); return false; }

	const long size = FileSize(part);
	const std::string sha = FileSha256(part);
	if (size != ROM_SIZE || sha != ROM_SHA256) {
		LOG_MSG("AWE32: the downloaded file has %ld bytes, SHA-256 %s - rejected", size, sha.c_str());
		remove(part.c_str());
		err = MSG_Get("AWE32ROM_BADFILE");
		return false;
	}
	remove(dest.c_str());
	if (rename(part.c_str(), dest.c_str()) != 0) {
		remove(part.c_str());
		err = "cannot rename " + part;
		return false;
	}
	path = dest;
	return true;
}

std::string Format(const char *fmt, const std::string &a, const std::string &b) {
	std::vector<char> buf(strlen(fmt) + a.size() + b.size() + 16);
	snprintf(buf.data(), buf.size(), fmt, a.c_str(), b.c_str());
	return std::string(buf.data());
}

} // anonymous namespace

void AWE32ROM_AddMessages(void) {
	MSG_Add("AWE32ROM_TITLE", "Sound Blaster AWE32 wave ROM");
	MSG_Add("AWE32ROM_ASK",
		"The Sound Blaster AWE32 emulation (sbtype=sbawe) needs the wave ROM of the "
		"EMU8000 chip (awe32.raw, 1 MB). It is not distributed with DOSBox-X.\n\n"
		"Download it now from\n%s\ninto\n%s ?\n\n"
		"Without it the AWE32 plays only sounds that programs load into its RAM; "
		"the General MIDI sounds of the ROM stay silent.");
	MSG_Add("AWE32ROM_FAILED",
		"The AWE32 wave ROM could not be downloaded:\n%s\n\n"
		"You can download awe32.raw yourself from\n%s\n"
		"and put it into the folder AWE32ROM next to DOSBox-X, or set its path "
		"with the option awe32rom= in the [sblaster] section.");
	MSG_Add("AWE32ROM_BADFILE",
		"The downloaded file is not the expected AWE32 wave ROM (the size or the SHA-256 checksum does not match).");
}

std::string AWE32ROM_Locate(const std::string &configured, const std::string &download_mode) {
	AWE32ROM_AddMessages();

	if (!configured.empty()) {
		std::string p = configured;
		Cross::ResolveHomedir(p);
		if (IsUsableRom(p)) return p;
		LOG_MSG("AWE32: awe32rom=%s is missing or shorter than 1 MB, looking elsewhere", p.c_str());
	}

	const std::string exe_dir = ExecutableDir();
	std::string conf_dir = Cross::GetPlatformConfigDir();
	std::vector<std::string> dirs;
	if (!exe_dir.empty()) dirs.push_back(Join(exe_dir, ROM_DIR));
	if (!conf_dir.empty()) dirs.push_back(Join(conf_dir, ROM_DIR));
	dirs.push_back(ROM_DIR);

	for (size_t i = 0; i < dirs.size(); i++) {
		const std::string p = Join(dirs[i], ROM_FILE);
		if (IsUsableRom(p)) {
			const std::string sha = FileSha256(p);
			if (sha != ROM_SHA256)
				LOG_MSG("AWE32: %s is not the known ROM image (SHA-256 %s); using it anyway", p.c_str(), sha.c_str());
			return p;
		}
	}

	/* Not found: download? */
	if (download_mode == "no" || asked_this_session) return std::string();

	/* Next to the executable, as the other files of this build; the
	 * configuration directory when that one is read-only (installed builds). */
	std::string target;
	if (!exe_dir.empty() && Writable(Join(exe_dir, ROM_DIR))) target = Join(exe_dir, ROM_DIR);
	else if (!conf_dir.empty()) { Cross::CreatePlatformConfigDir(); target = Join(conf_dir, ROM_DIR); }
	else target = ROM_DIR;

	asked_this_session = true;
	if (download_mode != "yes") {
		/* no dialog in silent/test runs */
		if (control != NULL && (control->opt_silent || control->opt_test)) return std::string();
		const std::string q = Format(MSG_Get("AWE32ROM_ASK"), AWE32ROM_URL, target);
		if (!systemmessagebox(MSG_Get("AWE32ROM_TITLE"), q.c_str(), "yesno", "question", 1))
			return std::string();
	}

	std::string path, err;
	if (DownloadRom(target, path, err)) {
		LOG_MSG("AWE32: wave ROM saved to %s", path.c_str());
		return path;
	}
	LOG_MSG("AWE32: wave ROM download failed: %s", err.c_str());
	const std::string msg = Format(MSG_Get("AWE32ROM_FAILED"), err, AWE32ROM_URL);
	systemmessagebox(MSG_Get("AWE32ROM_TITLE"), msg.c_str(), "ok", "error", 1);
	return std::string();
}
