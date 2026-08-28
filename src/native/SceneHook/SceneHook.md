# SceneHook

A drop-in header that lets any number of Windower addons draw inside FFXI's 3D
scene at the same time.

Authored by Broguypal. BSD 3-Clause. Copy this folder into your addon and go.

## Why

`FFXiMain!draw_scene` has one prologue, so only one addon can patch it. Addons
that work around this by chaining into each other end up holding raw pointers
into modules that Lua can unload at any moment, which crashes the game.
SceneHook patches `draw_scene` once, in memory owned by nobody, and hands out
callbacks instead.

## What you get

- Load and unload addons in any order, as often as you like.
- No module pinning, so no DLL file is locked and `//lua reload` works normally.
- Every registered addon draws in the same frame; output composites naturally.
- Participants never see or reason about each other.

## Using it

Add the folder to your include path:

```cmake
target_include_directories(_MyAddon PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../SceneHook")
```

```cpp
#include "SceneHook.h"

static SceneBus* g_bus = nullptr;
static int g_slot = -1;
```

Write a draw callback. It runs inside `draw_scene` with the depth buffer still
bound, so geometry you emit is occluded by the world as you would expect.
`renderer` is FFXi's renderer pointer; `device` is best-effort and may be null,
in which case resolve your own `IDirect3DDevice8` from `renderer`.

```cpp
void SCENEHOOK_ALIGN_STACK __cdecl my_draw(void* user, void* renderer, void* device) {
    // draw whatever you like
}
```

Join the hook when your module starts:

```cpp
g_bus = scenehook_attach();
if (g_bus && scenehook_ensure_hook(g_bus) && g_slot < 0) {
    g_slot = scenehook_register(g_bus, &my_draw, nullptr);
}
```

`scenehook_ensure_hook` installs the patch if it is not already there. It is
cheap and safe to call from every module on every load; only the first caller
does any work.

Stop drawing without leaving:

```cpp
scenehook_set_enabled(g_bus, g_slot, false);
```

Leave, from `DllMain`, unconditionally:

```cpp
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
        scenehook_unregister(g_bus, g_slot, false);
        g_slot = -1;
    }
    return TRUE;
}
```

**This call is the one that must not be skipped.** `DLL_PROCESS_DETACH` is the
only teardown point guaranteed to run however your addon goes away — a normal
unload, a force-unload, or a Lua error during unload — and it runs before your
image is unmapped. Do not move it into an unload event handler. The `false`
argument means "do not wait": the loader lock is held there, so only lock-free
stores are performed.

## Two obligations

**Leave device state as you found it.** Several addons draw back to back in one
frame. If you change a render state and do not restore it, whoever draws next
inherits it. An `IDirect3DStateBlock8` — `CreateStateBlock(D3DSBT_ALL)` once,
`Capture()` before, `Apply()` after — is the clean way to stay independent.

**Keep the stack aligned.** `SCENEHOOK_ALIGN_STACK` matters on GCC/MinGW builds:
FFXi's render thread does not guarantee a 16-byte aligned stack and SSE spills
will fault without it. It expands to nothing on MSVC.

## How it works

`draw_scene` is patched exactly once with an indirect jump:

```
draw_scene:   FF 25 <&owner_ptr>   90 90 90        (6 + 3 = 9 bytes)
```

`owner_ptr` and the trampoline holding the displaced prologue live in a single
`VirtualAlloc` block that belongs to no module and is never freed, so the patch
does not depend on any DLL staying mapped.

```
owner_ptr == trampoline        nobody is drawing; draw_scene behaves
                               exactly as it always did
owner_ptr == a module's stub   that module drives the frame: it calls the
                               trampoline, then every registered callback
```

Handing the frame between modules is a single 4-byte aligned pointer store,
which is atomic on x86. When the module that installed the patch unloads, its
`DllMain` stores a surviving client's stub into `owner_ptr` — or the trampoline
if it was the last one out — before its image unmaps. Nothing is ever
unpatched, so unload order cannot matter.

Modules find each other through a named file mapping keyed on process ID. Its
handle and view are leaked deliberately: both belong to the process rather than
to any module, which is what lets the hook outlive every participant and still
be there when one comes back. Total cost is one page and a handle for the life
of the client.

## Limits

Sixty-four clients, first come first served.

An addon that patches `draw_scene` itself, without using SceneHook, will still
conflict. The failure mode is a message rather than a crash: whichever side
loses reports `draw_scene signature not found` and draws nothing.

Every participant must build against a byte-identical copy of `SceneHook.h`. The
ABI version is checked at runtime, and a module that meets a bus built from a
different version refuses to join rather than guess at the layout. `ready`,
`abi_version` and `size` sit at fixed offsets and never move; anything else may
only be appended to.

32-bit only, which a `static_assert` enforces.
