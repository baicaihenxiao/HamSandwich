#include "appdata.h"
#include "log.h"
#include <string>

#ifdef SDL_UNPREFIXED
	#include <SDL_platform.h>
	#include <SDL_rwops.h>
#else  // SDL_UNPREFIXED
	#include <SDL2/SDL_platform.h>
	#include <SDL2/SDL_rwops.h>
#endif  // SDL_UNPREFIXED

// Common code
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#define MKDIR_MODE 0777

static int mkdir_one(const char *path, mode_t mode) {
	if (mkdir(path, mode) != 0 && errno != EEXIST)
		return -1;
	return 0;
}

static int mkdir_parents(const char *path, mode_t mode) {
	char *copypath = strdup(path);
	char *start = copypath;
	char *next;

	int status = 0;
	while (status == 0 && (next = strchr(start, '/'))) {
		if (next != start) {
			// skip the root directory and double-slashes
			*next = '\0';
			status = mkdir_one(copypath, mode);
			*next = '/';
		}
		start = next + 1;
	}

	free(copypath);

	if (status != 0)
		LogError("mkdirs(%s): %s", path, strerror(errno));
	return status;
}

static bool is_write_mode(const char* mode) {
	return mode[0] == 'w' || mode[0] == 'a' || (mode[0] == 'r' && mode[1] == '+');
}
#endif

// TODO: re-enable this when there's some means of porting existing installs.
#if 0  // #ifdef _WIN32
// Windows ----------------------------------------------------------

#include <io.h>
#include <shlobj.h> // for SHGetFolderPath
#ifdef _MSC_VER
#include <direct.h>
#endif

FILE* AppdataOpen(const char* file, const char* mode) {
	char get_folder_path[MAX_PATH];
	SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, get_folder_path);

	std::string buffer = get_folder_path;
	buffer.append("\\Hamumu\\");
	buffer.append(AppdataFolderName());
	buffer.append("\\");
	buffer.append(file);
	if (is_write_mode(mode)) {
		mkdir_parents(file, MKDIR_MODE);  // TODO: mode is meaningless on win32
	}
	return fopen(file, mode);
}

FILE* AssetOpen(const char* file, const char* mode) {
	return fopen(file, mode);
}

SDL_RWops* AssetOpen_SDL(const char* file, const char* mode) {
	return SDL_RWFromFile(file, mode);
}

#elif defined(__EMSCRIPTEN__)
// Emscripten -------------------------------------------------------

#include <emscripten.h>

FILE* AppdataOpen(const char* file, const char* mode) {
	std::string buffer = "/appdata/";
	buffer.append(AppdataFolderName());
	buffer.append("/");
	buffer.append(file);

	if (is_write_mode(mode)) {
		mkdir_parents(buffer.c_str(), MKDIR_MODE);
	}
	FILE* fp = fopen(buffer.c_str(), mode);
	if (!fp) {
		LogDebug("AppdataOpen(%s, %s): %s", file, mode, strerror(errno));
	}
	return fp;
}

FILE* AssetOpen(const char* file, const char* mode) {
	if (is_write_mode(mode)) {
		return AppdataOpen(file, mode);
	}
	return fopen(file, mode);
}

SDL_RWops* AssetOpen_SDL(const char* file, const char* mode) {
	if (is_write_mode(mode)) {
		LogDebug("AssetOpen_SDL(%s, %s) -> AppdataOpen", file, mode);
		FILE *fp = AppdataOpen(file, mode);
		if (fp) {
            return SDL_RWFromFP(fp, SDL_TRUE);
		} else {
			return nullptr;
		}
	} else {
		// Will try to read from Android internal storage, or else
		// pull from the asset system.
		return SDL_RWFromFile(file, mode);
	}
}

void AppdataSync() {
	EM_ASM(
		Module.fsSave();
	);
}

#elif defined(__ANDROID__) && __ANDROID__
// Android ----------------------------------------------------------

#include <SDL_system.h>
#include <string.h>

FILE* AppdataOpen(const char* file, const char* mode) {
	std::string buffer;

	// Only use external storage if it is both readable and writeable
	int need_flags = SDL_ANDROID_EXTERNAL_STORAGE_READ | SDL_ANDROID_EXTERNAL_STORAGE_WRITE;
	if ((SDL_AndroidGetExternalStorageState() & need_flags) == need_flags) {
		buffer = SDL_AndroidGetExternalStoragePath();
	} else {
		buffer = SDL_AndroidGetInternalStoragePath();
	}

	buffer.append("/");
	buffer.append(file);
	if (is_write_mode(mode)) {
		mkdir_parents(buffer.c_str(), MKDIR_MODE);
	}
	FILE* fp = fopen(buffer.c_str(), mode);
	if (!fp) {
		LogDebug("AppdataOpen(%s, %s): %s", file, mode, strerror(errno));
	}
	return fp;
}

FILE* AssetOpen(const char* file, const char* mode) {
	LogDebug("AssetOpen(%s, %s)", file, mode);
	if (is_write_mode(mode)) {
		return AppdataOpen(file, mode);
	}

	// Check internal storage to see if we've already extracted the file.
	char fname_buf[1024];
	sprintf(fname_buf, "%s/%s", SDL_AndroidGetInternalStoragePath(), file);
	unlink(fname_buf);

	// Not in internal storage, so ask SDL to pull it from the asset system.
	SDL_RWops *rw = SDL_RWFromFile(file, mode);
	if (!rw) {
		LogError("AssetOpen(%s) bad SDL: %s", file, SDL_GetError());
		return nullptr;
	}

	mkdir_parents(fname_buf, MKDIR_MODE);
	FILE* fp = fopen(fname_buf, "wb");
	if (!fp) {
		LogError("AssetOpen(%s) bad save: %s", file, strerror(errno));
		return nullptr;
	}

	// Copy everything.
	char buffer[4096];
	int read;
	while ((read = SDL_RWread(rw, buffer, 1, sizeof(buffer))) > 0) {
		fwrite(buffer, 1, read, fp);
	}

	// Return a FILE* pointing to the extracted asset.
	fclose(fp);
	fp = fopen(fname_buf, mode);
	if (!fp) {
		LogError("AssetOpen(%s) bad readback: %s", file, strerror(errno));
	}
	return fp;
}

SDL_RWops* AssetOpen_SDL(const char* file, const char* mode) {
	if (is_write_mode(mode)) {
		LogDebug("AssetOpen_SDL(%s, %s) -> AppdataOpen", file, mode);
		FILE *fp = AppdataOpen(file, mode);
		if (fp) {
            return SDL_RWFromFP(fp, SDL_TRUE);
		} else {
			return nullptr;
		}
	} else {
		// Will try to read from Android internal storage, or else
		// pull from the asset system.
		return SDL_RWFromFile(file, mode);
	}
}

void AppdataSync() {
}

#else
// Default ----------------------------------------------------------

FILE* AppdataOpen(const char* file, const char* mode) {
	return fopen(file, mode);
}

FILE* AssetOpen(const char* file, const char* mode) {
	return fopen(file, mode);
}

SDL_RWops* AssetOpen_SDL(const char* file, const char* mode) {
	return SDL_RWFromFile(file, mode);
}

void AppdataSync() {
}

#endif
