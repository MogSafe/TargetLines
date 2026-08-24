# TargetLines v2 Native Addon Module

This directory contains the supported Windower 4 Lua C module used by
TargetLines v2. `TargetLines.lua` loads the module from the addon's `libs`
folder with `require('_TargetLines')`; Windower's legacy plugin system is not
used.

The module joins the shared SceneHook ABI v2, resolves and validates the D3D8
device from FFXI's renderer, and runs the TargetLines line and ring renderer
from the shared scene callback. Lua publishes complete encoded snapshots into
an inactive native buffer, and an atomic swap exposes each completed snapshot
to the renderer without JSON files, filesystem polling, or partial reads.

## Runtime location

```text
addons/TargetLines/libs/_TargetLines.dll
```

Users install the complete `addons/TargetLines` directory and load only:

```text
lua load TargetLines
```

## Build

Build the required 32-bit DLL from the repository root with the MSYS2 MinGW32
toolchain:

```powershell
mingw32 -c "g++ -shared -static -static-libgcc -static-libstdc++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -m32 -Isrc/native -o addons/TargetLines/libs/_TargetLines.dll src/native/TargetLinesModule.cpp src/native/TargetLinesRenderer.cpp src/native/exports.def -ld3d8"
```

The release module must be PE32/i386 and export only:

```text
luaopen__TargetLines
```

Static MinGW runtime linkage keeps the runtime import surface limited to
Windows system libraries. Replacing the live DLL requires FFXI to be closed so
the loaded module is no longer mapped.

## Lua API

```lua
local native = require('_TargetLines')
native.start()
native.status()
native.bind_state('Phoenix-Character')
native.replace_state(encoded_state)
native.stop()
native.version()
```

- `start()` initializes the renderer and registers its SceneHook client.
- `status()` reports lifecycle, SceneHook ownership, callback count, renderer
  and device state, process-local identity, and state-publication count.
- `bind_state()` supplies a validated server-character identity for status and
  diagnostics; it does not select or create a state file.
- `replace_state()` parses and atomically publishes a complete immutable render
  snapshot.
- `stop()` unregisters the callback, waits for in-flight rendering to finish,
  and releases renderer resources before the module can be unloaded.
- `version()` returns the native module version used by Lua compatibility
  checks.

The renderer captures every D3D8 state it changes, including stream 0 because
`DrawPrimitiveUP` clears that binding, and restores the captured state before
returning to the game or another SceneHook client. Failed state capture or
configuration disables that draw pass rather than drawing with partial state.

## Diagnostics

Native diagnostics are written in the module's addon folder as
`addons/TargetLines/native.log`. The log is capped at 5 MiB and rotates to
`native.log.1`.

In game, use:

```text
//tl nativestatus
```

## SceneHook dependency

`SceneHook/SceneHook.h` is the byte-identical ABI v2 header published by
Broguypal on August 19, 2026. The vendored header, documentation, and BSD
3-Clause license are retained in `SceneHook/`. Binary distributions reproduce
the license in `addons/TargetLines/THIRD_PARTY_NOTICES.md`.

SceneHook is a community integration layer, not an official Windower rendering
API. Its shared ABI allows compatible clients such as TargetLines and
TargetRing to register independently, transfer frame ownership safely, and
unregister callbacks before their DLLs are unmapped.
