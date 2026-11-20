// Nube simulada: SOLO PARA PRUEBAS en la copia isaac_repentogon. Nunca se instala en el juego real.
//
// Sustituye, dentro de este proceso, las funciones de archivos de la nube de Steam
// (ISteamRemoteStorage v016, entradas 0-23 de su tabla virtual) por versiones que leen y escriben
// en una carpeta local: partidas_pruebas/fake_cloud/. Así se puede probar el juego con
// "SteamCloud=1" sin que ninguna lectura ni escritura llegue a la nube real.
// Cada operación se apunta en mas_ranuras.log con el prefijo [nube].

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace {

std::string g_dir;           // carpeta de la nube simulada (termina en '/')
void** g_vtable = nullptr;   // copia de la tabla virtual con las entradas sustituidas
void* g_object = nullptr;    // objeto real de ISteamRemoteStorage
CRITICAL_SECTION g_lock;

void Log(const char* fmt, ...) {
	FILE* f = nullptr;
	if (fopen_s(&f, "more_save_slots.log", "a") != 0 || !f) return;
	SYSTEMTIME t;
	GetLocalTime(&t);
	fprintf(f, "[%02d:%02d:%02d.%03d] [fake cloud] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
	va_list a;
	va_start(a, fmt);
	vfprintf(f, fmt, a);
	va_end(a);
	fputc('\n', f);
	fclose(f);
}

std::string PathOf(const char* name) { return g_dir + (name ? name : ""); }

// Las funciones de la interfaz son __thiscall: se imitan con __fastcall y un segundo registro sin usar.
#define SELFARGS void* self, void* edx

// E/S con las funciones de Windows de este módulo (no con el CRT): Más Ranuras redirige los archivos
// que abre el CRT del juego, y la nube real de Steam (steamclient) no pasa por esa redirección.
bool __fastcall FileWrite(SELFARGS, const char* name, const void* data, int32_t size) {
	HANDLE h = CreateFileA(PathOf(name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	DWORD written = 0;
	bool ok = h != INVALID_HANDLE_VALUE && WriteFile(h, data, (DWORD)size, &written, nullptr) && written == (DWORD)size;
	if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
	Log("FileWrite %s (%d bytes) -> %s", name, size, ok ? "ok" : "FALLO");
	return ok;
}
int32_t __fastcall FileRead(SELFARGS, const char* name, void* data, int32_t size) {
	HANDLE h = CreateFileA(PathOf(name).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	DWORD n = 0;
	if (h != INVALID_HANDLE_VALUE) { ReadFile(h, data, (DWORD)size, &n, nullptr); CloseHandle(h); }
	Log("FileRead %s -> %d bytes", name, (int)n);
	return (int32_t)n;
}
uint64_t __fastcall FileWriteAsync(SELFARGS, const char* name, const void*, uint32_t size) {
	Log("FileWriteAsync %s (%u bytes) -> NO SOPORTADO", name, size);
	return 0;
}
uint64_t __fastcall FileReadAsync(SELFARGS, const char* name, uint32_t, uint32_t) {
	Log("FileReadAsync %s -> NO SOPORTADO", name);
	return 0;
}
bool __fastcall FileReadAsyncComplete(SELFARGS, uint64_t, void*, uint32_t) { return false; }
bool __fastcall FileForget(SELFARGS, const char* name) { Log("FileForget %s", name); return true; }
bool __fastcall FileDelete(SELFARGS, const char* name) {
	bool ok = DeleteFileA(PathOf(name).c_str()) != 0;
	Log("FileDelete %s -> %s", name, ok ? "ok" : "no existia");
	return ok;
}
uint64_t __fastcall FileShare(SELFARGS, const char* name) { Log("FileShare %s", name); return 0; }
bool __fastcall SetSyncPlatforms(SELFARGS, const char*, int) { return true; }

std::map<uint64_t, std::pair<std::string, std::vector<char>>> g_streams;
uint64_t g_nextStream = 1;
uint64_t __fastcall StreamOpen(SELFARGS, const char* name) {
	EnterCriticalSection(&g_lock);
	uint64_t h = g_nextStream++;
	g_streams[h] = { name ? name : "", {} };
	LeaveCriticalSection(&g_lock);
	Log("FileWriteStreamOpen %s", name);
	return h;
}
bool __fastcall StreamWrite(SELFARGS, uint64_t h, const void* data, int32_t size) {
	EnterCriticalSection(&g_lock);
	auto it = g_streams.find(h);
	bool ok = it != g_streams.end();
	if (ok) it->second.second.insert(it->second.second.end(), (const char*)data, (const char*)data + size);
	LeaveCriticalSection(&g_lock);
	return ok;
}
bool __fastcall StreamClose(SELFARGS, uint64_t h) {
	EnterCriticalSection(&g_lock);
	auto it = g_streams.find(h);
	bool ok = it != g_streams.end();
	std::string name;
	std::vector<char> buf;
	if (ok) { name = it->second.first; buf.swap(it->second.second); g_streams.erase(it); }
	LeaveCriticalSection(&g_lock);
	return ok && FileWrite(self, edx, name.c_str(), buf.data(), (int32_t)buf.size());
}
bool __fastcall StreamCancel(SELFARGS, uint64_t h) {
	EnterCriticalSection(&g_lock);
	g_streams.erase(h);
	LeaveCriticalSection(&g_lock);
	return true;
}
bool __fastcall FileExists(SELFARGS, const char* name) {
	DWORD a = GetFileAttributesA(PathOf(name).c_str());
	bool ok = a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
	Log("FileExists %s -> %d", name, ok ? 1 : 0);
	return ok;
}
bool __fastcall FilePersisted(SELFARGS, const char* name) { return FileExists(self, edx, name); }
int32_t __fastcall GetFileSize(SELFARGS, const char* name) {
	WIN32_FILE_ATTRIBUTE_DATA d;
	int32_t n = GetFileAttributesExA(PathOf(name).c_str(), GetFileExInfoStandard, &d) ? (int32_t)d.nFileSizeLow : 0;
	Log("GetFileSize %s -> %d", name, n);
	return n;
}
int64_t __fastcall GetFileTimestamp(SELFARGS, const char* name) {
	WIN32_FILE_ATTRIBUTE_DATA d;
	if (!GetFileAttributesExA(PathOf(name).c_str(), GetFileExInfoStandard, &d)) return 0;
	ULARGE_INTEGER u;
	u.LowPart = d.ftLastWriteTime.dwLowDateTime;
	u.HighPart = d.ftLastWriteTime.dwHighDateTime;
	return (int64_t)((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
}
int __fastcall GetSyncPlatforms(SELFARGS, const char*) { return -1; }

std::vector<std::pair<std::string, int32_t>> ListFiles() {
	std::vector<std::pair<std::string, int32_t>> out;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((g_dir + "*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return out;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back({ fd.cFileName, (int32_t)fd.nFileSizeLow });
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	return out;
}
int32_t __fastcall GetFileCount(SELFARGS) {
	int32_t n = (int32_t)ListFiles().size();
	Log("GetFileCount -> %d", n);
	return n;
}
std::string g_nameBuf;
const char* __fastcall GetFileNameAndSize(SELFARGS, int i, int32_t* size) {
	auto files = ListFiles();
	if (i < 0 || i >= (int)files.size()) { if (size) *size = 0; return ""; }
	g_nameBuf = files[i].first;
	if (size) *size = files[i].second;
	return g_nameBuf.c_str();
}
bool __fastcall GetQuota(SELFARGS, uint64_t* total, uint64_t* avail) {
	if (total) *total = 100ull * 1024 * 1024;
	if (avail) *avail = 99ull * 1024 * 1024;
	return true;
}
bool __fastcall IsCloudEnabledForAccount(SELFARGS) { return true; }
bool __fastcall IsCloudEnabledForApp(SELFARGS) { return true; }
void __fastcall SetCloudEnabledForApp(SELFARGS, bool) {}

void* g_replacements[24] = {
	(void*)&FileWrite, (void*)&FileRead, (void*)&FileWriteAsync, (void*)&FileReadAsync,
	(void*)&FileReadAsyncComplete, (void*)&FileForget, (void*)&FileDelete, (void*)&FileShare,
	(void*)&SetSyncPlatforms, (void*)&StreamOpen, (void*)&StreamWrite, (void*)&StreamClose,
	(void*)&StreamCancel, (void*)&FileExists, (void*)&FilePersisted, (void*)&GetFileSize,
	(void*)&GetFileTimestamp, (void*)&GetSyncPlatforms, (void*)&GetFileCount, (void*)&GetFileNameAndSize,
	(void*)&GetQuota, (void*)&IsCloudEnabledForAccount, (void*)&IsCloudEnabledForApp, (void*)&SetCloudEnabledForApp,
};

// Cambia la tabla virtual del objeto real por una copia con las entradas 0-23 sustituidas.
void Install(void* object) {
	if (!object || object == g_object) return;
	void** realVtable = *(void***)object;
	if (realVtable == g_vtable) return;
	const int kSize = 64;  // la interfaz tiene menos entradas; se copian de sobra sin pasarse del módulo
	g_vtable = new void*[kSize];
	MEMORY_BASIC_INFORMATION mbi;
	VirtualQuery(realVtable, &mbi, sizeof(mbi));
	size_t avail8 = ((char*)mbi.BaseAddress + mbi.RegionSize - (char*)realVtable) / sizeof(void*);
	size_t copy = avail8 < (size_t)kSize ? avail8 : (size_t)kSize;
	memcpy(g_vtable, realVtable, copy * sizeof(void*));
	for (int i = 0; i < 24; i++) g_vtable[i] = g_replacements[i];
	DWORD old;
	VirtualProtect(object, sizeof(void*), PAGE_READWRITE, &old);
	*(void***)object = g_vtable;
	VirtualProtect(object, sizeof(void*), old, &old);
	g_object = object;
	Log("Instalada sobre ISteamRemoteStorage016 %p. Carpeta: %s", object, g_dir.c_str());
}

using FindIfaceFn = void*(__cdecl*)(int, const char*);
FindIfaceFn g_realFind = nullptr;

void* __cdecl FindIfaceHook(int user, const char* version) {
	void* obj = g_realFind(user, version);
	if (version && strcmp(version, "STEAMREMOTESTORAGE_INTERFACE_VERSION016") == 0) {
		EnterCriticalSection(&g_lock);
		Install(obj);
		LeaveCriticalSection(&g_lock);
	}
	return obj;
}

bool PatchImport(HMODULE module, const char* dll, const char* func, void* hook, void** original) {
	auto base = (BYTE*)module;
	auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
	auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
		if (_stricmp((const char*)(base + imp->Name), dll) != 0) continue;
		auto names = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
		auto addrs = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
		for (; names->u1.AddressOfData; names++, addrs++) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
			if (strcmp((const char*)((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name, func) != 0) continue;
			DWORD old;
			VirtualProtect(&addrs->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
			*original = (void*)addrs->u1.Function;
			addrs->u1.Function = (DWORD_PTR)hook;
			VirtualProtect(&addrs->u1.Function, sizeof(void*), old, &old);
			return true;
		}
	}
	return false;
}

}  // namespace

extern "C" __declspec(dllexport) int ModInit(int, char**) {
	InitializeCriticalSection(&g_lock);
	char dllPath[MAX_PATH] = {};
	HMODULE self = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&ModInit, &self);
	GetModuleFileNameA(self, dllPath, MAX_PATH);
	std::string dir(dllPath);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);  // ...\isaac_repentogon\Repentogon\ .
	g_dir = dir + "..\\..\\partidas_pruebas\\fake_cloud\\";
	char full[MAX_PATH];
	GetFullPathNameA(g_dir.c_str(), MAX_PATH, full, nullptr);
	g_dir = full;
	CreateDirectoryA(g_dir.c_str(), nullptr);

	bool hooked = PatchImport(GetModuleHandleA(nullptr), "steam_api.dll", "SteamInternal_FindOrCreateUserInterface",
		(void*)&FindIfaceHook, (void**)&g_realFind);
	// Si el juego ya pidió la interfaz antes de cargar este DLL, se instala ahora.
	HMODULE steam = GetModuleHandleA("steam_api.dll");
	auto getUser = steam ? (int(__cdecl*)())GetProcAddress(steam, "SteamAPI_GetHSteamUser") : nullptr;
	int user = getUser ? getUser() : 0;
	Log("=== Nube simulada cargada (hook %s, usuario Steam %s) ===", hooked ? "ok" : "NO", user ? "ya iniciado" : "aun no");
	if (user && g_realFind) {
		EnterCriticalSection(&g_lock);
		Install(g_realFind(user, "STEAMREMOTESTORAGE_INTERFACE_VERSION016"));
		LeaveCriticalSection(&g_lock);
	}
	return hooked ? 0 : 1;  // sin el hook no se arranca: mejor no abrir el juego que tocar la nube real
}
