// More Save Slots 1.0.0
// Copyright (C) 2026 MaflaTV
//
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation.
// It is built against REPENTOGON's libzhl (https://github.com/TeamREPENTOGON/REPENTOGON).
//
// Adds pages of 3 save slots to The Binding of Isaac: Repentance+ save menu (REPENTOGON), up to 9.
//   Page 1  = slots 1-3 (the usual ones, with Steam Cloud if enabled).
//   Pages 2-3 = slots 4-6 and 7-9, always local (never written to Steam Cloud).
//
// Save menu controls:
//   Right on file 3   -> next page (or the "+" tab on the last page)
//   Left on file 1    -> previous page
//   LB / RB           -> previous / next page
//   Confirm on "+"    -> adds a page (max 3)
//
// How it works: the game, REPENTOGON and mods always see slots 1-3 (their internal tables and
// REPENTOGON only accept 1-3). Only the file names on disk change: while a slot from pages 2-3 is
// active, every access to a "slot N" file (progress, run in progress, REPENTOGON extra data, mod data)
// is redirected to the real slot. The redirect lives in the Windows file functions used by the game's
// own modules (never Steam's, never the system's).

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwctype>
#include <ctime>
#include <set>
#include <string>

#include "IsaacRepentance.h"
#include "HookSystem.h"
#include "Log.h"

// Exported so the installer can read them from the DLL file (single source of truth).
// MoreSaveSlotsSupportedZhl: comma-separated libzhl versions whose structure layouts this build was
// compiled against. With any other version the mod stays inactive (all hooks pass through) instead of
// risking save data.
extern "C" {
__declspec(dllexport) const char* MoreSaveSlotsVersion = "1.0.0";
#ifdef MSS_TEST_UNSUPPORTED  // test build: behaves as if the installed REPENTOGON were unsupported
__declspec(dllexport) const char* MoreSaveSlotsSupportedZhl = "0.0.0";
#else
__declspec(dllexport) const char* MoreSaveSlotsSupportedZhl = "1.1.3";
#endif
}

namespace {

const char* const kModVersion = MoreSaveSlotsVersion;
const std::string kSupportedText = std::string("REPENTOGON ") + MoreSaveSlotsSupportedZhl;

bool IsSupportedZhl(const std::string& v) {
	std::string list = std::string(",") + MoreSaveSlotsSupportedZhl + ",";
	return !v.empty() && list.find("," + v + ",") != std::string::npos;
}

constexpr int kSlotsPerPage = 3;
constexpr int kMaxPages = 3;
constexpr int kMenuSaves = 2;            // MenuManager::_selectedMenuID of the save menu
constexpr int kActConfirm = 14, kActBack = 15;
constexpr int kActLeft = 20, kActRight = 21, kActLB = 24, kActRB = 25;
constexpr int kHookPriority = -100;      // runs before REPENTOGON's own hooks (priority 0)
constexpr int kSlideFrames = 10;         // frames per half of the slide animation
constexpr float kSlideDistance = 480.f;  // width of the menu canvas
constexpr int kSoundSlide = 569;         // SOUND_MENU_FLIP_LIGHT
constexpr int kSoundAdd = 17;            // SOUND_PAPER_IN
constexpr int kSoundTab = 282;           // SOUND_MENU_SCROLL

bool g_enabled = false;
std::string g_reason = "not initialized";
std::string g_zhlVersion = "unknown";
std::string g_rgonVersion = "unknown";

int g_pages = 1;          // available pages (1-3)
int g_page = 0;           // page on screen (0-2)
unsigned g_realSlot = 0;  // real active slot (0 = none, 1-9)
bool g_shutdown = false;
bool g_plusSelected = false;
bool g_blockInput = false;
std::string g_assetDir;   // the mod's graphics folder (next to the DLL)

struct Slide {
	bool active = false;
	int dir = 0;          // +1 = towards the next page
	int frame = 0;
	bool swapped = false;
	int targetPage = 0;
	int targetSel = 0;
	float base = 0.f;
	float offset = 0.f;
} g_slide;

// ---------------------------------------------------------------- utilities

constexpr const char* kLogFile = "more_save_slots.log";

void Log(const char* fmt, ...) {
	FILE* f = nullptr;
	if (fopen_s(&f, kLogFile, "a") != 0 || !f) return;
	time_t now = time(nullptr);
	tm local{};
	localtime_s(&local, &now);
	fprintf(f, "[%02d:%02d:%02d] ", local.tm_hour, local.tm_min, local.tm_sec);
	va_list args;
	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);
	fputc('\n', f);
	fclose(f);
}

std::string ExportedString(const char* module, const char* symbol) {
	HMODULE m = GetModuleHandleA(module);
	auto p = m ? (const char**)GetProcAddress(m, symbol) : nullptr;
	return (p && *p) ? std::string(*p) : std::string("unknown");
}

Menu_Save* SaveMenu() { return g_MenuManager ? g_MenuManager->GetMenuSave() : nullptr; }

bool InSaveMenu() {
	return g_MenuManager != nullptr && !Isaac::IsInGame() && g_MenuManager->_selectedMenuID == kMenuSaves;
}

// ---------------------------------------------------------------- file redirection

// Slot shift to apply right now (0 = no redirect) and which digit is redirected (0 = any of 1-3).
int CurrentShift(int& onlyDigit) {
	onlyDigit = 0;
	if (!g_enabled || g_shutdown) return 0;
	if (g_realSlot > (unsigned)kSlotsPerPage) {
		onlyDigit = (int)((g_realSlot - 1) % kSlotsPerPage) + 1;
		return (int)(g_realSlot - onlyDigit);
	}
	// In the save menu with no slot loaded (e.g. "Delete file"), follow the page on screen.
	if (g_realSlot == 0 && g_page > 0 && InSaveMenu()) return g_page * kSlotsPerPage;
	return 0;
}

template <class C> C Lower(C c) { return (C)towlower((wint_t)c); }

template <class S>
bool EndsWithI(const S& s, size_t end, const char* suffix) {
	size_t n = strlen(suffix);
	if (end < n) return false;
	for (size_t i = 0; i < n; i++)
		if (Lower(s[end - n + i]) != (typename S::value_type)suffix[i]) return false;
	return true;
}

// "Slot" files that get redirected (the digit sits right before the extension):
//   ...persistentgamedataN.dat   (rgon_, rep+, REPENTOGON backups)
//   ...gamestateN.dat / .json, ...rerunstateN.dat / .json  (run in progress and REPENTOGON extra data)
//   completionmarksN.json, achievementsN.json  (REPENTOGON)
//   data/<mod>/saveN.dat  (mod data)
template <class S>
bool MapSlotPath(const S& path, S& out) {
	int onlyDigit = 0;
	const int shift = CurrentShift(onlyDigit);
	if (shift == 0 || path.size() < 8) return false;
	size_t slash = path.find_last_of((typename S::value_type)'/');
	size_t bslash = path.find_last_of((typename S::value_type)'\\');
	size_t nameStart = 0;
	if (slash != S::npos) nameStart = slash + 1;
	if (bslash != S::npos && bslash + 1 > nameStart) nameStart = bslash + 1;
	size_t dot = path.find_last_of((typename S::value_type)'.');
	if (dot == S::npos || dot <= nameStart + 1) return false;
	const auto digitChar = path[dot - 1];
	if (digitChar < '1' || digitChar > '3') return false;
	const int digit = (int)(digitChar - '0');
	if (onlyDigit && digit != onlyDigit) return false;

	const bool isDat = EndsWithI(path, path.size(), ".dat");
	const bool isJson = EndsWithI(path, path.size(), ".json");
	if (!isDat && !isJson) return false;
	const size_t stemEnd = dot - 1;  // position of the digit
	bool match =
		(isDat && EndsWithI(path, stemEnd, "persistentgamedata")) ||
		EndsWithI(path, stemEnd, "gamestate") || EndsWithI(path, stemEnd, "rerunstate") ||
		(isJson && (EndsWithI(path, stemEnd, "completionmarks") || EndsWithI(path, stemEnd, "achievements")));
	if (!match && isDat && stemEnd - nameStart == 4 && EndsWithI(path, stemEnd, "save") && nameStart >= 2) {
		// data/<mod>/saveN.dat: the grandparent folder is "data"
		size_t prev = S::npos;
		for (size_t i = nameStart - 1; i-- > 0;) {
			if (path[i] == '/' || path[i] == '\\') { prev = i; break; }
		}
		if (prev != S::npos && prev >= 4 && EndsWithI(path, prev, "data")) {
			size_t before = prev - 4;
			match = before == 0 || path[before - 1] == '/' || path[before - 1] == '\\';
		}
	}
	if (!match) return false;
	out = path;
	out[stemEnd] = (typename S::value_type)('0' + digit + shift);
	return true;
}

struct MappedA {
	std::string buf;
	const char* p;
	explicit MappedA(const char* in) : p(in) {
		if (in && MapSlotPath(std::string(in), buf)) p = buf.c_str();
	}
};
struct MappedW {
	std::wstring buf;
	const wchar_t* p;
	explicit MappedW(const wchar_t* in) : p(in) {
		if (in && MapSlotPath(std::wstring(in), buf)) p = buf.c_str();
	}
};

#define REAL(name) decltype(&name) Real_##name = &name
REAL(CreateFileA); REAL(CreateFileW);
REAL(DeleteFileA); REAL(DeleteFileW);
REAL(MoveFileExA); REAL(MoveFileExW);
REAL(CopyFileA); REAL(CopyFileW);
REAL(GetFileAttributesA); REAL(GetFileAttributesW);
REAL(GetFileAttributesExA); REAL(GetFileAttributesExW);
REAL(FindFirstFileA); REAL(FindFirstFileW);
REAL(FindFirstFileExA); REAL(FindFirstFileExW);

HANDLE WINAPI Hook_CreateFileA(LPCSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t) { MappedA m(p); return Real_CreateFileA(m.p, a, s, sa, c, f, t); }
HANDLE WINAPI Hook_CreateFileW(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t) { MappedW m(p); return Real_CreateFileW(m.p, a, s, sa, c, f, t); }
BOOL WINAPI Hook_DeleteFileA(LPCSTR p) { MappedA m(p); return Real_DeleteFileA(m.p); }
BOOL WINAPI Hook_DeleteFileW(LPCWSTR p) { MappedW m(p); return Real_DeleteFileW(m.p); }
BOOL WINAPI Hook_MoveFileExA(LPCSTR a, LPCSTR b, DWORD f) { MappedA ma(a), mb(b); return Real_MoveFileExA(ma.p, mb.p, f); }
BOOL WINAPI Hook_MoveFileExW(LPCWSTR a, LPCWSTR b, DWORD f) { MappedW ma(a), mb(b); return Real_MoveFileExW(ma.p, mb.p, f); }
BOOL WINAPI Hook_CopyFileA(LPCSTR a, LPCSTR b, BOOL f) { MappedA ma(a), mb(b); return Real_CopyFileA(ma.p, mb.p, f); }
BOOL WINAPI Hook_CopyFileW(LPCWSTR a, LPCWSTR b, BOOL f) { MappedW ma(a), mb(b); return Real_CopyFileW(ma.p, mb.p, f); }
DWORD WINAPI Hook_GetFileAttributesA(LPCSTR p) { MappedA m(p); return Real_GetFileAttributesA(m.p); }
DWORD WINAPI Hook_GetFileAttributesW(LPCWSTR p) { MappedW m(p); return Real_GetFileAttributesW(m.p); }
BOOL WINAPI Hook_GetFileAttributesExA(LPCSTR p, GET_FILEEX_INFO_LEVELS l, LPVOID i) { MappedA m(p); return Real_GetFileAttributesExA(m.p, l, i); }
BOOL WINAPI Hook_GetFileAttributesExW(LPCWSTR p, GET_FILEEX_INFO_LEVELS l, LPVOID i) { MappedW m(p); return Real_GetFileAttributesExW(m.p, l, i); }
HANDLE WINAPI Hook_FindFirstFileA(LPCSTR p, LPWIN32_FIND_DATAA d) { MappedA m(p); return Real_FindFirstFileA(m.p, d); }
HANDLE WINAPI Hook_FindFirstFileW(LPCWSTR p, LPWIN32_FIND_DATAW d) { MappedW m(p); return Real_FindFirstFileW(m.p, d); }
HANDLE WINAPI Hook_FindFirstFileExA(LPCSTR p, FINDEX_INFO_LEVELS l, LPVOID d, FINDEX_SEARCH_OPS o, LPVOID f, DWORD x) { MappedA m(p); return Real_FindFirstFileExA(m.p, l, d, o, f, x); }
HANDLE WINAPI Hook_FindFirstFileExW(LPCWSTR p, FINDEX_INFO_LEVELS l, LPVOID d, FINDEX_SEARCH_OPS o, LPVOID f, DWORD x) { MappedW m(p); return Real_FindFirstFileExW(m.p, l, d, o, f, x); }

struct ApiHook {
	const char* name;
	void* hook;
	void** real;
	void* targets[2];  // address in kernel32 and in kernelbase
};
#define API(n) { #n, (void*)&Hook_##n, (void**)&Real_##n, { nullptr, nullptr } }
ApiHook g_apis[] = {
	API(CreateFileA), API(CreateFileW), API(DeleteFileA), API(DeleteFileW),
	API(MoveFileExA), API(MoveFileExW), API(CopyFileA), API(CopyFileW),
	API(GetFileAttributesA), API(GetFileAttributesW), API(GetFileAttributesExA), API(GetFileAttributesExW),
	API(FindFirstFileA), API(FindFirstFileW), API(FindFirstFileExA), API(FindFirstFileExW),
};

void ResolveApis() {
	HMODULE k32 = GetModuleHandleA("kernel32.dll");
	HMODULE kb = GetModuleHandleA("kernelbase.dll");
	for (ApiHook& a : g_apis) {
		a.targets[0] = k32 ? (void*)GetProcAddress(k32, a.name) : nullptr;
		a.targets[1] = kb ? (void*)GetProcAddress(kb, a.name) : nullptr;
		// The originals always go to kernelbase when it exists (never back through our own hook).
		*a.real = a.targets[1] ? a.targets[1] : a.targets[0];
	}
}

// Replaces, in one module's import table, every entry that points to one of the APIs above.
int PatchModuleIat(HMODULE module) {
	auto base = (BYTE*)module;
	auto dos = (IMAGE_DOS_HEADER*)base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
	auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
	auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress) return 0;
	int patched = 0;
	for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
		for (auto thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk); thunk->u1.Function; thunk++) {
			void* fn = (void*)thunk->u1.Function;
			for (ApiHook& a : g_apis) {
				if (fn != a.targets[0] && fn != a.targets[1]) continue;
				DWORD old;
				if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
					thunk->u1.Function = (DWORD_PTR)a.hook;
					VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
					patched++;
				}
				break;
			}
		}
	}
	return patched;
}

// Only the game's modules: the executable, REPENTOGON and the C/C++ runtimes they open files with.
// Never Steam (its cloud files share the save names) and never the system.
bool IsGameModule(const wchar_t* n) {
	static const wchar_t* names[] = { L"isaac-ng.exe", L"ucrtbase.dll", L"msvcp140.dll", L"msvcp140_1.dll",
		L"msvcp140_2.dll", L"libzhl.dll", L"zhlREPENTOGON.dll" };
	for (const wchar_t* x : names)
		if (!_wcsicmp(n, x)) return true;
	return false;
}

void PatchAllModules() {
	if (!g_enabled) return;
	static std::set<HMODULE> done;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
	if (snap == INVALID_HANDLE_VALUE) return;
	MODULEENTRY32W me{};
	me.dwSize = sizeof(me);
	for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
		if (done.count(me.hModule) || !IsGameModule(me.szModule)) continue;
		done.insert(me.hModule);
		int count = PatchModuleIat(me.hModule);
		if (count) Log("File redirect installed in %ls (%d imports)", me.szModule, count);
	}
	CloseHandle(snap);
}

// ---------------------------------------------------------------- config and backup

std::string SaveDir() { return std::string(&g_SaveDataPath); }  // ends with '/'
std::string ConfigPath() { return SaveDir() + "Repentogon/more_save_slots.ini"; }

bool SlotExists(int slot) {
	std::string p = SaveDir() + "Repentogon/rgon_persistentgamedata" + std::to_string(slot) + ".dat";
	DWORD a = Real_GetFileAttributesA(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Copies every file of a folder (not recursive) into dest. Uses the real APIs, never the redirect.
int CopyFolderFiles(const std::string& src, const std::string& dest) {
	WIN32_FIND_DATAA fd;
	HANDLE h = Real_FindFirstFileA((src + "*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return 0;
	int n = 0;
	CreateDirectoryA(dest.c_str(), nullptr);
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		if (Real_CopyFileA((src + fd.cFileName).c_str(), (dest + fd.cFileName).c_str(), FALSE)) n++;
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	return n;
}

// One-time safety copy of the save folder the first time the mod runs on this profile.
void BackupSavesOnce() {
	const std::string ini = ConfigPath();
	if (GetPrivateProfileIntA("MoreSaveSlots", "BackupDone", 0, ini.c_str())) return;
	time_t now = time(nullptr);
	tm local{};
	localtime_s(&local, &now);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
	const std::string root = SaveDir() + "Repentogon/more_save_slots_backup/";
	const std::string dest = root + stamp + "/";
	CreateDirectoryA(root.c_str(), nullptr);
	CreateDirectoryA(dest.c_str(), nullptr);
	int n = CopyFolderFiles(SaveDir(), dest);
	n += CopyFolderFiles(SaveDir() + "Repentogon/", dest + "Repentogon/");
	for (const char* sub : { "EntitySaveStateManagement", "ItemPoolManager", "VirtualRoomSetManager" }) {
		n += CopyFolderFiles(SaveDir() + "Repentogon/" + sub + "/", dest + "Repentogon/" + sub + "/");
	}
	WritePrivateProfileStringA("MoreSaveSlots", "BackupDone", "1", ini.c_str());
	Log("First run: backed up %d save files to %s", n, dest.c_str());
}

void LoadConfig() {
	const std::string ini = ConfigPath();
	g_pages = GetPrivateProfileIntA("MoreSaveSlots", "Pages", 1, ini.c_str());
	g_page = GetPrivateProfileIntA("MoreSaveSlots", "LastPage", 0, ini.c_str());
	// Never hide slots that already exist on disk.
	for (int slot = kSlotsPerPage + 1; slot <= kSlotsPerPage * kMaxPages; slot++) {
		if (SlotExists(slot)) {
			int needed = (slot - 1) / kSlotsPerPage + 1;
			if (needed > g_pages) g_pages = needed;
		}
	}
	if (g_pages < 1) g_pages = 1;
	if (g_pages > kMaxPages) g_pages = kMaxPages;
	if (g_page < 0 || g_page >= g_pages) g_page = 0;
	Log("Config: %d page(s), last page %d", g_pages, g_page + 1);
}

// The save folder is set by the game after mods load, so it is read on first use.
void EnsureConfig() {
	static bool loaded = false;
	if (!g_enabled || loaded || (&g_SaveDataPath)[0] == '\0') return;
	loaded = true;
	BackupSavesOnce();
	LoadConfig();
}

void SaveConfig() {
	const std::string ini = ConfigPath();
	WritePrivateProfileStringA("MoreSaveSlots", "Pages", std::to_string(g_pages).c_str(), ini.c_str());
	WritePrivateProfileStringA("MoreSaveSlots", "LastPage", std::to_string(g_page).c_str(), ini.c_str());
}

// ---------------------------------------------------------------- Lua global for the Workshop companion

// MoreSaveSlots = { Version, Active, Reason, ZHLVersion, REPENTOGONVersion, Supported }
// Set with the Lua C API of whichever Lua DLL the game uses; re-created if Lua restarts.
struct LuaApi {
	int (__cdecl* getglobal)(void*, const char*) = nullptr;
	void (__cdecl* settop)(void*, int) = nullptr;
	void (__cdecl* createtable)(void*, int, int) = nullptr;
	const char* (__cdecl* pushstring)(void*, const char*) = nullptr;
	void (__cdecl* pushboolean)(void*, int) = nullptr;
	void (__cdecl* setfield)(void*, int, const char*) = nullptr;
	void (__cdecl* setglobal)(void*, const char*) = nullptr;
	bool ok = false;
} g_lua;

void ResolveLua() {
	if (g_lua.ok) return;
	HMODULE m = GetModuleHandleA("Lua5.4.dll");
	if (!m) m = GetModuleHandleA("Lua5.3.3r.dll");
	if (!m) return;
	g_lua.getglobal = (decltype(g_lua.getglobal))GetProcAddress(m, "lua_getglobal");
	g_lua.settop = (decltype(g_lua.settop))GetProcAddress(m, "lua_settop");
	g_lua.createtable = (decltype(g_lua.createtable))GetProcAddress(m, "lua_createtable");
	g_lua.pushstring = (decltype(g_lua.pushstring))GetProcAddress(m, "lua_pushstring");
	g_lua.pushboolean = (decltype(g_lua.pushboolean))GetProcAddress(m, "lua_pushboolean");
	g_lua.setfield = (decltype(g_lua.setfield))GetProcAddress(m, "lua_setfield");
	g_lua.setglobal = (decltype(g_lua.setglobal))GetProcAddress(m, "lua_setglobal");
	g_lua.ok = g_lua.getglobal && g_lua.settop && g_lua.createtable && g_lua.pushstring && g_lua.pushboolean &&
		g_lua.setfield && g_lua.setglobal;
}

void EnsureLuaGlobal() {
	if (!g_LuaEngine) return;
	void* L = g_LuaEngine->_state;
	if (!L) return;
	ResolveLua();
	if (!g_lua.ok) return;
	if (g_rgonVersion == "unknown") g_rgonVersion = ExportedString("zhlREPENTOGON.dll", "__REPENTOGON_VERSION");
	const int type = g_lua.getglobal(L, "MoreSaveSlots");
	g_lua.settop(L, -2);
	if (type != 0) return;  // already set (0 = nil)
	g_lua.createtable(L, 0, 6);
	g_lua.pushstring(L, kModVersion);          g_lua.setfield(L, -2, "Version");
	g_lua.pushboolean(L, g_enabled ? 1 : 0);   g_lua.setfield(L, -2, "Active");
	g_lua.pushstring(L, g_reason.c_str());     g_lua.setfield(L, -2, "Reason");
	g_lua.pushstring(L, g_zhlVersion.c_str()); g_lua.setfield(L, -2, "ZHLVersion");
	g_lua.pushstring(L, g_rgonVersion.c_str()); g_lua.setfield(L, -2, "REPENTOGONVersion");
	g_lua.pushstring(L, kSupportedText.c_str()); g_lua.setfield(L, -2, "Supported");
	g_lua.setglobal(L, "MoreSaveSlots");
}

// ---------------------------------------------------------------- menu

void PlaySound(int id) { g_Manager->_sfxManager.Play(id, 1.0f, 0, false, 1.0f, 0.f); }

// Save menu sheet with the "FILE N" labels of the current page (layers File1-File3).
void ApplyPageSheet() {
	Menu_Save* ms = SaveMenu();
	if (!ms) return;
	ANM2* sprite = &ms->SaveSelectMenuSprite;
	std::string path = g_page == 0 ? std::string("gfx/ui/main menu/saveselectmenu.png")
	                                : g_assetDir + "saveselectmenu_p" + std::to_string(g_page + 1) + ".png";
	bool ok = true;
	for (int layer = 1; layer <= 3; layer++) ok = sprite->ReplaceSpritesheet(layer, path) && ok;
	sprite->LoadGraphics(false);
	if (!ok) Log("Could not swap the save menu sheet for page %d (%s)", g_page + 1, path.c_str());
}

void PlayFileAnim(int sel, bool select) {
	Menu_Save* ms = SaveMenu();
	if (!ms || sel < 0 || sel > 2) return;
	char anim[32];
	sprintf_s(anim, "File%d%s", sel + 1, select ? "Select" : "Deselect");
	ms->SaveSelectMenuSprite.Play(anim, true);
}

void StartSlide(int dir, int targetPage, int targetSel) {
	Menu_Save* ms = SaveMenu();
	if (!ms || g_slide.active) return;
	g_slide = Slide{};
	g_slide.active = true;
	g_slide.dir = dir;
	g_slide.targetPage = targetPage;
	g_slide.targetSel = targetSel;
	g_slide.base = ms->PositionX;
	PlaySound(kSoundSlide);
}

// Advances the animation: the current page slides out, the slots are swapped, the new page slides in.
void UpdateSlide() {
	Menu_Save* ms = SaveMenu();
	if (!g_slide.active || !ms) return;
	g_slide.frame++;
	if (!g_slide.swapped) {
		float t = (float)g_slide.frame / kSlideFrames;
		g_slide.offset = -g_slide.dir * kSlideDistance * t * t;  // accelerates out
		if (g_slide.frame >= kSlideFrames) {
			g_page = g_slide.targetPage;
			SaveConfig();
			ms->Reset();  // reloads the 3 slots of the new page
			ms->SelectedSave = g_slide.targetSel;
			PlayFileAnim(g_slide.targetSel, true);
			g_slide.swapped = true;
			g_slide.frame = 0;
			g_slide.offset = g_slide.dir * kSlideDistance;
		}
	} else {
		float t = (float)g_slide.frame / kSlideFrames;
		float e = 1.f - (1.f - t) * (1.f - t);  // slows down in
		g_slide.offset = g_slide.dir * kSlideDistance * (1.f - e);
		if (g_slide.frame >= kSlideFrames) {
			g_slide.active = false;
			g_slide.offset = 0.f;
		}
	}
	ms->PositionX = g_slide.base + g_slide.offset;
}

ANM2* g_tabs = nullptr;
bool g_tabsFailed = false;

void DrawFrame(const char* anim, Vector pos) {
	std::string name(anim);
	g_tabs->SetFrame(&name, 0);
	Vector zero(0, 0);
	g_tabs->Render(&pos, &zero, &zero);
}

void RenderOverlay() {
	Menu_Save* ms = SaveMenu();
	if (!ms || !InSaveMenu() || ms->State != 0) return;
	if (!g_tabs && !g_tabsFailed) {
		g_tabs = new ANM2();
		std::string path = g_assetDir + "more_save_slots.anm2";
		g_tabs->Load(path, true);
		if (!g_tabs->_loaded) {
			g_tabsFailed = true;
			Log("Could not load %s", path.c_str());
			return;
		}
	}
	if (!g_tabs || g_tabsFailed) return;

	const float W = g_WIDTH, H = g_HEIGHT;
	// Camera offset relative to the resting menu (without the slide), so the tabs travel with the
	// menu when the camera pans into it.
	const Vector& view = g_MenuManager->_ViewPosition;
	float dx = (ms->PositionX - g_slide.offset + view.x) + (480.f - W) / 2.f;
	float dy = (ms->PositionY + view.y) + (270.f - H) / 2.f;

	// Tabs on the screen edges at mid height, like bookmarks.
	const float tabY = H * 0.5f - 10.f + dy;
	if (g_page > 0) DrawFrame("Prev", Vector(10.f + dx, tabY));
	if (g_page < g_pages - 1) DrawFrame("Next", Vector(W - 10.f + dx, tabY));
	else if (g_pages < kMaxPages) DrawFrame(g_plusSelected ? "PlusSel" : "Plus", Vector(W - (g_plusSelected ? 18.f : 10.f) + dx, tabY));

	// Page dots between the cards and "Delete file" (anchored to the 480x270 menu canvas).
	if (g_pages > 1) {
		float x0 = W * 0.5f - (g_pages - 1) * 6.5f;
		float y = 217.f + ms->PositionY + view.y;
		for (int i = 0; i < g_pages; i++) {
			DrawFrame(i == g_page ? "DotOn" : "Dot", Vector(x0 + i * 13.f + dx, y));
		}
	}
}

// Slots 4-9 only exist with REPENTOGON, which blocks online play, and in Repentance+ daily runs live
// inside the online mode. These achievements cannot be earned there, and without them Dead God (every
// achievement) and the infinity banner would be impossible:
//   325 Dedication (31 dailies) · 336 The Marathon (5-win daily streak) · 354 Broken Modem (7 dailies)
//   638 Play Online · 639 Win Online · 640 Win Online Daily
// They are set directly in the save data (no TryUnlock: no popup, nothing sent to Steam).
constexpr int kUnreachableAchievements[] = { 325, 336, 354, 638, 639, 640 };

void MarkUnreachableAchievements(PersistentGameData* pgd) {
	if (!pgd) return;
	int marked = 0;
	for (int id : kUnreachableAchievements) {
		if (!pgd->achievements[id]) {
			pgd->achievements[id] = true;
			marked++;
		}
	}
	if (marked) {
		pgd->Save();
		Log("Slot %u: marked %d online/daily achievements", g_realSlot, marked);
	}
}

// The engine picks cloud or local by reading OptionsConfig::_enableSteamCloud directly (it does not
// always call Manager::IsSteamCloudEnabled). While a slot 4-9 is active, or the save menu shows page
// 2-3 ("Delete file" deletes without loading the slot), the flag is turned off so nothing (engine,
// REPENTOGON, mods) reads or writes the cloud with the visible slot number.
bool g_cloudForcedOff = false;
bool g_cloudOriginal = false;

void ApplyCloudOverride() {
	if (!g_Manager || !g_enabled) return;
	OptionsConfig* opts = g_Manager->GetOptions();
	const bool menuPage = g_realSlot == 0 && g_page > 0 && InSaveMenu();
	const bool wantOff = (g_realSlot > (unsigned)kSlotsPerPage || menuPage) && !g_shutdown;
	if (wantOff && !g_cloudForcedOff) {
		g_cloudOriginal = opts->_enableSteamCloud;
		opts->_enableSteamCloud = false;
		g_cloudForcedOff = true;
		if (g_cloudOriginal) Log("Steam Cloud paused for local slots");
	} else if (!wantOff && g_cloudForcedOff) {
		opts->_enableSteamCloud = g_cloudOriginal;
		g_cloudForcedOff = false;
		if (g_cloudOriginal) Log("Steam Cloud restored");
	}
}

}  // namespace

// ---------------------------------------------------------------- hooks
// Every hook passes straight through when the mod is inactive (unsupported REPENTOGON version).

// options.ini is always saved with the real cloud setting, even while it is paused for slots 4-9.
HOOK_METHOD_PRIORITY(OptionsConfig, Save, kHookPriority, () -> void) {
	if (!g_cloudForcedOff) return super();
	this->_enableSteamCloud = g_cloudOriginal;
	super();
	this->_enableSteamCloud = false;
}

HOOK_METHOD_PRIORITY(Manager, SetSaveSlot, kHookPriority, (unsigned int slot) -> void) {
	if (!g_enabled) return super(slot);
	EnsureConfig();
	PatchAllModules();  // in case a module loaded later (REPENTOGON loads after this DLL)
	if (slot == 0) {
		super(slot);
		g_realSlot = 0;
		ApplyCloudOverride();
		return;
	}
	g_realSlot = (slot <= (unsigned)kSlotsPerPage) ? slot + kSlotsPerPage * g_page : slot;
	ApplyCloudOverride();
	super(slot);  // the game always sees 1-3; the files are redirected to the real slot
	if (g_realSlot > (unsigned)kSlotsPerPage) MarkUnreachableAchievements(this->GetPersistentGameData());
}

HOOK_METHOD_PRIORITY(Manager, IsSteamCloudEnabled, kHookPriority, () -> bool) {
	if (g_enabled && g_realSlot > (unsigned)kSlotsPerPage && !g_shutdown) return false;
	return super();
}

// On shutdown REPENTOGON syncs slots 1-3 with the Repentance+ saves: the active slot is unloaded first
// and the redirect turned off, so it works on the real slots.
HOOK_METHOD_PRIORITY(Manager, destructor, kHookPriority, () -> void) {
	if (g_enabled && g_realSlot > (unsigned)kSlotsPerPage) this->SetSaveSlot(0);
	g_shutdown = true;
	if (g_enabled) ApplyCloudOverride();
	super();
}

HOOK_METHOD_PRIORITY(Menu_Save, Reset, kHookPriority, () -> void) {
	super();
	if (g_enabled) ApplyPageSheet();
}

HOOK_METHOD_PRIORITY(Menu_Save, select_save_slot, kHookPriority, (uint32_t slot) -> void) {
	if (g_enabled) SaveConfig();
	super(slot);
}

HOOK_METHOD_PRIORITY(InputManager, IsActionTriggered, kHookPriority, (int btn, int controllerid, int unk) -> bool) {
	if (g_blockInput) return false;
	return super(btn, controllerid, unk);
}

HOOK_METHOD_PRIORITY(InputManager, IsActionPressed, kHookPriority, (int btn, int controllerid, int unk) -> bool) {
	if (g_blockInput) return false;
	return super(btn, controllerid, unk);
}

HOOK_METHOD_PRIORITY(MenuManager, Update, kHookPriority, () -> void) {
	EnsureLuaGlobal();
	if (!g_enabled) return super();
	EnsureConfig();
	Menu_Save* ms = SaveMenu();
	bool consume = false;
	if (!InSaveMenu() || !ms) {
		g_plusSelected = false;
	} else if (g_slide.active) {
		consume = true;
	} else if (ms->State == 0) {
		auto pressed = [](int a) { return g_InputManagerBase.IsActionTriggered(a, -1, 0); };
		const int sel = ms->SelectedSave;
		if (g_plusSelected) {
			consume = true;
			if (pressed(kActConfirm)) {
				g_plusSelected = false;
				g_pages++;
				SaveConfig();
				PlaySound(kSoundAdd);
				Log("Page added: %d page(s) now", g_pages);
				StartSlide(+1, g_pages - 1, 0);
			} else if (pressed(kActLeft) || pressed(kActBack)) {
				g_plusSelected = false;
				PlayFileAnim(2, true);
				PlaySound(kSoundTab);
			}
		} else if (sel == 2 && pressed(kActRight)) {
			consume = true;
			if (g_page < g_pages - 1) {
				StartSlide(+1, g_page + 1, 0);
			} else if (g_pages < kMaxPages) {
				g_plusSelected = true;
				PlaySound(kSoundTab);
			} else {
				consume = false;
			}
		} else if (sel == 0 && pressed(kActLeft) && g_page > 0) {
			consume = true;
			StartSlide(-1, g_page - 1, 2);
		} else if (pressed(kActRB) && g_page < g_pages - 1) {
			consume = true;
			StartSlide(+1, g_page + 1, sel >= 0 && sel <= 2 ? sel : 0);
		} else if (pressed(kActLB) && g_page > 0) {
			consume = true;
			StartSlide(-1, g_page - 1, sel >= 0 && sel <= 2 ? sel : 0);
		}
	}
	UpdateSlide();
	ApplyCloudOverride();
	g_blockInput = consume;
	super();
	g_blockInput = false;
	// With the "+" tab selected, card 3 stays unhighlighted (the game re-highlights it every frame).
	if (g_plusSelected && ms) {
		std::string anim("File3Deselect");
		ms->SaveSelectMenuSprite.SetFrame(&anim, 4);
	}
}

HOOK_METHOD_PRIORITY(MenuManager, Render, kHookPriority, () -> void) {
	super();
	if (g_enabled) RenderOverlay();
}

extern "C" __declspec(dllexport) int ModInit(int argc, char** argv) {
	// A fresh log on every start.
	FILE* f = nullptr;
	if (fopen_s(&f, kLogFile, "w") == 0 && f) fclose(f);

	char dllPath[MAX_PATH] = {};
	HMODULE self = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&ModInit, &self);
	GetModuleFileNameA(self, dllPath, MAX_PATH);
	std::string dir(dllPath);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	for (char& c : dir) if (c == '\\') c = '/';
	g_assetDir = dir + "more-save-slots/";

	g_zhlVersion = ExportedString("libzhl.dll", "__ZHL_VERSION");
	g_enabled = IsSupportedZhl(g_zhlVersion);
	DWORD assets = GetFileAttributesA((g_assetDir + "more_save_slots.anm2").c_str());
	if (!g_enabled) {
		g_reason = "unsupported REPENTOGON version (" + g_zhlVersion + "), this build supports " + kSupportedText;
	} else if (assets == INVALID_FILE_ATTRIBUTES) {
		g_enabled = false;
		g_reason = "missing graphics folder " + g_assetDir;
	} else {
		g_reason = "ok";
	}
	Log("More Save Slots %s - libzhl %s - %s", kModVersion, g_zhlVersion.c_str(), g_enabled ? "active" : g_reason.c_str());
	if (g_enabled) {
		ResolveApis();
		PatchAllModules();
	}
	return 0;  // never block the game: an inactive mod simply does nothing
}
