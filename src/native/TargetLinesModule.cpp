#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "SceneHook/SceneHook.h"
#include "TargetLinesRenderer.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

static_assert(sizeof(void*) == 4, "_TargetLines must be built as a 32-bit DLL");

using lua_State = struct lua_State;
using lua_CFunction = int(__cdecl*)(lua_State*);

using fn_createtable = void(__cdecl*)(lua_State*, int, int);
using fn_pushcclosure = void(__cdecl*)(lua_State*, lua_CFunction, int);
using fn_setfield = void(__cdecl*)(lua_State*, int, char const*);
using fn_pushvalue = void(__cdecl*)(lua_State*, int);
using fn_pushstring = void(__cdecl*)(lua_State*, char const*);
using fn_tolstring = char const*(__cdecl*)(lua_State*, int, std::size_t*);

constexpr int kGlobalsIndex = -10002;
constexpr char kModuleVersion[] = "2.0.0";
constexpr std::size_t kStateCapacity = 131072;

struct LuaApi {
    fn_createtable createtable = nullptr;
    fn_pushcclosure pushcclosure = nullptr;
    fn_setfield setfield = nullptr;
    fn_pushvalue pushvalue = nullptr;
    fn_pushstring pushstring = nullptr;
    fn_tolstring tolstring = nullptr;

    bool ready() const {
        return createtable && pushcclosure && setfield && pushvalue && pushstring
            && tolstring;
    }
};

LuaApi g_lua {};
volatile LONG g_started = 0;
volatile LONG g_frame_count = 0;
volatile LONG g_renderer_seen = 0;
HMODULE g_module = nullptr;
SceneBus* g_scene_bus = nullptr;
int g_scene_slot = -1;
TargetLinesRenderer* g_renderer = nullptr;
IDirect3DDevice8* g_device = nullptr;
bool g_wrapped_device = false;
unsigned long g_device_frames = 0;
unsigned long g_device_attempts = 0;
char g_state_identifier[128] {};
char g_pending_state[kStateCapacity] {};
std::size_t g_pending_state_size = 0;
volatile LONG g_state_updates = 0;
char g_status[192] = "idle (scene hook not connected)";

constexpr std::size_t kRendererScanWindow = 0x8000;
constexpr std::size_t kDeviceVtableEntries = 90;
constexpr unsigned long kDeviceRetryFrames = 30;
constexpr unsigned long kMaxDeviceAttempts = 240;

bool bind_lua() {
    if (g_lua.ready()) {
        return true;
    }

    static char const* const hosts[] = {
        "LuaCore.dll",
        "lua51.dll",
        "lua5.1.dll",
    };

    for (char const* host : hosts) {
        HMODULE module = GetModuleHandleA(host);
        if (!module) {
            continue;
        }

        g_lua.createtable = reinterpret_cast<fn_createtable>(
            reinterpret_cast<void*>(GetProcAddress(module, "lua_createtable")));
        g_lua.pushcclosure = reinterpret_cast<fn_pushcclosure>(
            reinterpret_cast<void*>(GetProcAddress(module, "lua_pushcclosure")));
        g_lua.setfield = reinterpret_cast<fn_setfield>(
            reinterpret_cast<void*>(GetProcAddress(module, "lua_setfield")));
        g_lua.pushvalue = reinterpret_cast<fn_pushvalue>(
            reinterpret_cast<void*>(GetProcAddress(module, "lua_pushvalue")));
        g_lua.pushstring = reinterpret_cast<fn_pushstring>(
            reinterpret_cast<void*>(GetProcAddress(module, "lua_pushstring")));
        g_lua.tolstring = reinterpret_cast<fn_tolstring>(
            reinterpret_cast<void*>(GetProcAddress(module, "lua_tolstring")));

        if (g_lua.ready()) {
            return true;
        }

        g_lua = {};
    }

    return false;
}

// The renderer-to-device discovery strategy is adapted from Broguypal's
// TargetRing native module (BSD 3-Clause). It validates only the COM methods
// TargetLines needs so wrapped D3D8 devices remain supported.
bool page_readable(DWORD protect) {
    if (protect & (PAGE_GUARD | PAGE_NOACCESS)) {
        return false;
    }
    switch (protect & 0xFF) {
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

bool page_executable(DWORD protect) {
    if (protect & (PAGE_GUARD | PAGE_NOACCESS)) {
        return false;
    }
    switch (protect & 0xFF) {
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

bool span_readable(std::uintptr_t address, std::size_t size) {
    if (address == 0 || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION region {};
    if (!VirtualQuery(reinterpret_cast<void const*>(address), &region, sizeof(region))
        || region.State != MEM_COMMIT || !page_readable(region.Protect)) {
        return false;
    }
    std::uintptr_t const low = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    return address >= low && address + size <= low + region.RegionSize;
}

bool module_range(char const* name, std::uintptr_t& base, std::size_t& size) {
    base = 0;
    size = 0;
    HMODULE module = GetModuleHandleA(name);
    if (!module) {
        return false;
    }
    base = reinterpret_cast<std::uintptr_t>(module);
    if (!span_readable(base, sizeof(IMAGE_DOS_HEADER))) {
        return false;
    }
    auto const* dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    std::uintptr_t const nt_address = base + static_cast<std::uintptr_t>(dos->e_lfanew);
    if (!span_readable(nt_address, sizeof(IMAGE_NT_HEADERS32))) {
        return false;
    }
    auto const* nt = reinterpret_cast<IMAGE_NT_HEADERS32 const*>(nt_address);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    size = nt->OptionalHeader.SizeOfImage;
    return size != 0;
}

void* vtable_slot(std::uintptr_t object, int index) {
    if (!span_readable(object, sizeof(std::uintptr_t))) {
        return nullptr;
    }
    std::uintptr_t vtable = 0;
    std::memcpy(&vtable, reinterpret_cast<void const*>(object), sizeof(vtable));
    std::uintptr_t const entry =
        vtable + static_cast<std::uintptr_t>(index) * sizeof(std::uintptr_t);
    if (!span_readable(entry, sizeof(std::uintptr_t))) {
        return nullptr;
    }
    std::uintptr_t address = 0;
    std::memcpy(&address, reinterpret_cast<void const*>(entry), sizeof(address));
    return reinterpret_cast<void*>(address);
}

bool plausible_device_vtable(std::uintptr_t vtable) {
    if (!span_readable(vtable, sizeof(std::uintptr_t) * kDeviceVtableEntries)) {
        return false;
    }
    static int const probes[] = {2, 38, 41, 72};
    for (int slot : probes) {
        std::uintptr_t entry = 0;
        std::memcpy(&entry,
            reinterpret_cast<void const*>(vtable
                + static_cast<std::uintptr_t>(slot) * sizeof(std::uintptr_t)),
            sizeof(entry));
        MEMORY_BASIC_INFORMATION region {};
        if (entry == 0
            || !VirtualQuery(reinterpret_cast<void const*>(entry), &region, sizeof(region))
            || region.State != MEM_COMMIT || !page_executable(region.Protect)) {
            return false;
        }
    }
    return true;
}

using fn_get_device = long(__stdcall*)(void*, void**);
using fn_release = unsigned long(__stdcall*)(void*);

void acquire_device(std::uintptr_t renderer) {
    if (g_device || renderer == 0 || g_device_attempts >= kMaxDeviceAttempts) {
        return;
    }
    ++g_device_frames;
    if (g_device_attempts != 0 && (g_device_frames % kDeviceRetryFrames) != 0) {
        return;
    }
    ++g_device_attempts;

    std::uintptr_t d3d_base = 0;
    std::size_t d3d_size = 0;
    if (!module_range("d3d8.dll", d3d_base, d3d_size)) {
        return;
    }

    for (std::size_t offset = 0; offset + sizeof(std::uintptr_t) <= kRendererScanWindow;
            offset += sizeof(std::uintptr_t)) {
        std::uintptr_t const slot = renderer + offset;
        if (!span_readable(slot, sizeof(std::uintptr_t))) {
            continue;
        }
        std::uintptr_t resource = 0;
        std::memcpy(&resource, reinterpret_cast<void const*>(slot), sizeof(resource));
        if (!span_readable(resource, sizeof(std::uintptr_t))) {
            continue;
        }
        std::uintptr_t resource_vtable = 0;
        std::memcpy(&resource_vtable, reinterpret_cast<void const*>(resource),
            sizeof(resource_vtable));
        if (resource_vtable < d3d_base || resource_vtable >= d3d_base + d3d_size) {
            continue;
        }

        auto get_device = reinterpret_cast<fn_get_device>(vtable_slot(resource, 3));
        if (!get_device) {
            continue;
        }
        void* candidate = nullptr;
        if (get_device(reinterpret_cast<void*>(resource), &candidate) < 0 || !candidate) {
            continue;
        }

        std::uintptr_t const address = reinterpret_cast<std::uintptr_t>(candidate);
        auto release = reinterpret_cast<fn_release>(vtable_slot(address, 2));
        if (!span_readable(address, sizeof(std::uintptr_t))) {
            if (release) {
                release(candidate);
            }
            continue;
        }

        std::uintptr_t device_vtable = 0;
        std::memcpy(&device_vtable, candidate, sizeof(device_vtable));
        bool const same_module = device_vtable >= d3d_base
            && device_vtable < d3d_base + d3d_size;
        if (!same_module && !plausible_device_vtable(device_vtable)) {
            if (release) {
                release(candidate);
            }
            continue;
        }

        g_device = static_cast<IDirect3DDevice8*>(candidate);
        g_wrapped_device = !same_module;
        if (release) {
            release(candidate);
        }
        return;
    }
}

bool ensure_renderer() {
    if (!g_renderer) {
        g_renderer = targetlines_renderer_create(g_module);
        if (!g_renderer) {
            return false;
        }
        if (g_state_identifier[0] != '\0'
            && !targetlines_renderer_set_identity(g_renderer, g_state_identifier)) {
            targetlines_renderer_destroy(g_renderer);
            g_renderer = nullptr;
            return false;
        }
        if (g_pending_state_size > 0
            && !targetlines_renderer_replace_state(
                g_renderer, g_pending_state, g_pending_state_size)) {
            targetlines_renderer_destroy(g_renderer);
            g_renderer = nullptr;
            return false;
        }
    }
    return true;
}

void release_renderer() {
    if (g_renderer) {
        targetlines_renderer_destroy(g_renderer);
        g_renderer = nullptr;
    }
    g_device = nullptr;
    g_wrapped_device = false;
    g_device_frames = 0;
    g_device_attempts = 0;
}

void SCENEHOOK_ALIGN_STACK __cdecl scene_probe(
        void*, void* renderer, void*) {
    if (renderer) {
        InterlockedExchange(&g_renderer_seen, 1);
    }
    InterlockedIncrement(&g_frame_count);
    acquire_device(reinterpret_cast<std::uintptr_t>(renderer));
    targetlines_renderer_render(g_renderer, g_device);
}

bool start_scene_probe() {
    if (!ensure_renderer()) {
        std::snprintf(g_status, sizeof(g_status),
            "start failed (module=%s, renderer initialization failed)", kModuleVersion);
        return false;
    }
    if (!g_scene_bus) {
        g_scene_bus = scenehook_attach();
    }
    if (!g_scene_bus) {
        std::snprintf(g_status, sizeof(g_status),
            "start failed (module=%s, SceneHook ABI unavailable)", kModuleVersion);
        return false;
    }

    if (!scenehook_ensure_hook(g_scene_bus)) {
        std::snprintf(g_status, sizeof(g_status),
            "start failed (module=%s, %s)", kModuleVersion, g_scene_bus->status);
        return false;
    }

    if (g_scene_slot < 0) {
        g_scene_slot = scenehook_register(g_scene_bus, &scene_probe, nullptr);
        if (g_scene_slot < 0) {
            std::snprintf(g_status, sizeof(g_status),
                "start failed (module=%s, SceneHook client limit reached)",
                kModuleVersion);
            return false;
        }
    }

    InterlockedExchange(&g_frame_count, 0);
    InterlockedExchange(&g_renderer_seen, 0);
    scenehook_set_enabled(g_scene_bus, g_scene_slot, true);
    InterlockedExchange(&g_started, 1);
    std::snprintf(g_status, sizeof(g_status),
        "started (module=%s, SceneHook ABI %lu, renderer active)",
        kModuleVersion, static_cast<unsigned long>(SCENEHOOK_ABI_VERSION));
    return true;
}

int __cdecl lua_start(lua_State* state) {
    start_scene_probe();
    g_lua.pushstring(state, g_status);
    return 1;
}

int __cdecl lua_stop(lua_State* state) {
    scenehook_unregister(g_scene_bus, g_scene_slot, true);
    g_scene_slot = -1;
    InterlockedExchange(&g_started, 0);
    release_renderer();
    std::snprintf(g_status, sizeof(g_status),
        "stopped (module=%s, renderer released)", kModuleVersion);
    g_lua.pushstring(state, g_status);
    return 1;
}

int __cdecl lua_bind_state(lua_State* state) {
    char const* identifier = g_lua.tolstring(state, 1, nullptr);
    if (!identifier || identifier[0] == '\0' || std::strcmp(identifier, "off") == 0
        || std::strcmp(identifier, "none") == 0) {
        g_state_identifier[0] = '\0';
        targetlines_renderer_unbind_state(g_renderer);
        g_lua.pushstring(state, "state unbound");
        return 1;
    }

    std::snprintf(g_state_identifier, sizeof(g_state_identifier), "%s", identifier);
    bool const bound = !g_renderer
        || targetlines_renderer_set_identity(g_renderer, g_state_identifier);
    g_lua.pushstring(state, bound ? "state bound" : "state bind failed");
    return 1;
}

int __cdecl lua_replace_state(lua_State* state) {
    std::size_t state_size = 0;
    char const* encoded = g_lua.tolstring(state, 1, &state_size);
    if (!encoded || state_size == 0 || state_size >= kStateCapacity) {
        g_lua.pushstring(state, "state rejected: expected 1-131071 bytes");
        return 1;
    }

    std::memcpy(g_pending_state, encoded, state_size);
    g_pending_state[state_size] = '\0';
    g_pending_state_size = state_size;
    bool const replaced = !g_renderer
        || targetlines_renderer_replace_state(g_renderer, g_pending_state, g_pending_state_size);
    if (replaced) {
        InterlockedIncrement(&g_state_updates);
    }
    g_lua.pushstring(state, replaced ? "state replaced" : "state replace failed");
    return 1;
}

int __cdecl lua_status(lua_State* state) {
    char hook_status[224] {};
    if (g_scene_slot < 0) {
        int const clients = scenehook_active_clients(g_scene_bus);
        std::snprintf(hook_status, sizeof(hook_status), "not registered, %d active client%s",
            clients, clients == 1 ? "" : "s");
    } else {
        scenehook_describe(g_scene_bus, g_scene_slot, hook_status, sizeof(hook_status));
    }

    char renderer_status[320] {};
    targetlines_renderer_status(g_renderer, renderer_status, sizeof(renderer_status));

    char report[832] {};
    std::snprintf(report, sizeof(report),
        "module=%s, lifecycle=%s, renderer=scenehook-v%lu, frames=%ld, "
        "renderer_seen=%s, device=%s, state_updates=%ld, %s | %s",
        kModuleVersion,
        InterlockedCompareExchange(&g_started, 0, 0) ? "started" : "stopped",
        static_cast<unsigned long>(SCENEHOOK_ABI_VERSION),
        InterlockedCompareExchange(&g_frame_count, 0, 0),
        InterlockedCompareExchange(&g_renderer_seen, 0, 0) ? "yes" : "no",
        g_device ? (g_wrapped_device ? "wrapped" : "native") : "pending",
        InterlockedCompareExchange(&g_state_updates, 0, 0),
        hook_status, renderer_status);
    g_lua.pushstring(state, report);
    return 1;
}

int __cdecl lua_version(lua_State* state) {
    g_lua.pushstring(state, kModuleVersion);
    return 1;
}

} // namespace

extern "C" __declspec(dllexport) int __cdecl luaopen__TargetLines(lua_State* state) {
    if (!state || !bind_lua()) {
        return 0;
    }

    if (g_scene_slot < 0) {
        InterlockedExchange(&g_started, 0);
        std::snprintf(g_status, sizeof(g_status),
            "idle (module=%s, scene hook not connected)", kModuleVersion);
    }

    g_lua.createtable(state, 0, 6);

    g_lua.pushcclosure(state, lua_start, 0);
    g_lua.setfield(state, -2, "start");

    g_lua.pushcclosure(state, lua_stop, 0);
    g_lua.setfield(state, -2, "stop");

    g_lua.pushcclosure(state, lua_status, 0);
    g_lua.setfield(state, -2, "status");

    g_lua.pushcclosure(state, lua_bind_state, 0);
    g_lua.setfield(state, -2, "bind_state");

    g_lua.pushcclosure(state, lua_replace_state, 0);
    g_lua.setfield(state, -2, "replace_state");

    g_lua.pushcclosure(state, lua_version, 0);
    g_lua.setfield(state, -2, "version");

    g_lua.pushvalue(state, -1);
    g_lua.setfield(state, kGlobalsIndex, "_TargetLines");
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH) {
        // This is the required final backstop: it hands frame ownership to a
        // surviving SceneHook client before this image is unmapped. Waiting is
        // forbidden here because the loader lock is held.
        scenehook_unregister(g_scene_bus, g_scene_slot, false);
        g_scene_slot = -1;
        // Normal addon unload calls stop() and releases the renderer after
        // draining the callback. Under the loader lock we only sever pointers;
        // attempting heap or handle teardown here would be unsafe.
        g_renderer = nullptr;
        g_device = nullptr;
        InterlockedExchange(&g_started, 0);
    }

    return TRUE;
}
