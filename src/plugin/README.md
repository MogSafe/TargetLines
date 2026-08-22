# TargetLines Native Plugin

This folder contains the native Windower plugin used by TargetLines. The Lua
addon writes line state to a server/character file under
`plugins/settings/TargetLines/instances`; this plugin binds to that file and
renders the lines with D3D8 (Direct3D 8). Per-character routing keeps multiple
game clients using the same Windower installation isolated from one another.

## Files

```text
TargetLines.cpp     Native plugin implementation: commands, state parsing, D3D8 rendering, anchors, diagnostics.
WindowerPlugin.h    Minimal Windower plugin interface definitions used to compile the DLL.
exports.def         DLL export list for Windower's required entry points.
CMakeLists.txt      Optional CMake project file for building the plugin.
README.md           Native plugin development notes.
```

## Build

From the repository root:

```powershell
mingw32 -c "g++ -shared -static -static-libgcc -static-libstdc++ -std=c++17 -Wall -Wextra -Wpedantic -m32 -o plugins/TargetLines.dll src/plugin/TargetLines.cpp src/plugin/exports.def -ld3d8"
```

The command above writes the rebuilt DLL to the repository copy:

```text
plugins\TargetLines.dll
```

## Runtime DLL location

Windower loads the active native plugin from the Windower plugin folder, not
from this repository's `plugins` folder.

```text
Windower\plugins\TargetLines.dll
```

After rebuilding, copy `plugins\TargetLines.dll` from the repository to
`Windower\plugins\TargetLines.dll` before testing in game. Unload the plugin
first so Windows can replace the DLL:

```text
//unload targetlines
```

In-game:

```text
//load targetlines
//lua l TargetLines
```

Native diagnostics are written to:

```text
plugins/settings/TargetLines/native.log
```

## Native Commands

```text
//targetlines status
//targetlines path
//targetlines statefile <server-character>
//targetlines statefile off
//targetlines drawon
//targetlines drawoff
//targetlines drawtest
//targetlines sourceheight -0.75
//targetlines targetheight -1.35
//targetlines height -1.0
//targetlines matrixprobe
//targetlines matrixnext
//targetlines ffxiprobe
//targetlines luamobprobe
```

`statefile` is normally managed by the Lua addon. It accepts an identifier,
not an arbitrary path, and is exposed for diagnostics and isolation testing.
