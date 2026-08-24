# Archived v1 Windower Plugin

This directory preserves the source for TargetLines' legacy v1 Windower
plugin for historical reference and regression investigation. It is not part
of the TargetLines v2 runtime or release package.

Do not install a DLL built from this directory and do not add
`load targetlines` to Windower startup scripts. TargetLines v2 is loaded only
as an addon:

```text
lua load TargetLines
```

The supported v2 native source and build instructions are under
`src/native`. Its Lua-loaded module is installed at:

```text
addons/TargetLines/libs/_TargetLines.dll
```

## Archived files

```text
TargetLines.cpp     Legacy plugin implementation.
WindowerPlugin.h    Legacy Windower plugin interface definitions.
exports.def         Legacy Windower plugin exports.
CMakeLists.txt      Historical plugin build project.
```

These files are retained only to help compare v1 behavior and investigate
upgrade issues. The repository no longer ships `plugins/TargetLines.dll`.
