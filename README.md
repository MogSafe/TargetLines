# TargetLines

<img width="640" height="360" alt="TargetLines combat demonstration" src="https://github.com/user-attachments/assets/96355259-5cf1-4f55-94dc-ba106f6d363c" />

TargetLines is a Windower 4 addon that draws Final Fantasy XII-style target
lines during combat. Lines show the flow of actions between players, party
members, trusts, pets, and enemies. Supported area actions can also be shown
with various AoE effects.

v2 packages its native 3D renderer as a Lua-loaded module inside
the addon. It does not require a Windower plugin or transient JSON state files.

Maintainer: [MogSafe](https://github.com/MogSafe)

## Installation

Download the TargetLines release ZIP and copy its `TargetLines` folder into
Windower's `addons` directory. The installed files should be:

```text
Windower/
└── addons/
    └── TargetLines/
        ├── TargetLines.lua
        ├── LICENSE.md
        ├── THIRD_PARTY_NOTICES.md
        └── libs/
            └── _TargetLines.dll
```

Load the addon in game:

```text
//lua load TargetLines
```

To load it automatically, add only this line to `Windower/scripts/init.txt` or
your profile's startup commands:

```text
lua load TargetLines
```

Nothing needs to be copied into `Windower/plugins`, and v2 must not have a
`load targetlines` plugin command in the startup configuration.

### Installing from Git

Clone the repository, then copy or link `addons/TargetLines` into Windower's
`addons` directory:

```powershell
git clone https://github.com/MogSafe/TargetLines.git
```

The repository also contains development source and documentation. Users need
only the `addons/TargetLines` folder.

### Upgrading from v1

Copy the new `addons/TargetLines` folder over the existing addon and remove the
old `load targetlines` command from startup scripts. Keep only:

```text
lua load TargetLines
```

On its first load, v2 checks for `Windower/plugins/TargetLines.dll`. If found,
it requests that Windower unload the legacy plugin and preserves the old DLL
as `TargetLines.dll.v1-disabled` (or a numbered variation) before starting the
v2 renderer. This prevents both renderers from running together and keeps a
recoverable v1 backup.

If the addon reports that it is waiting for the old plugin to unload, remove
the legacy startup command, restart Windower, and load the addon again.

Existing settings migrate automatically. V1 per-character JSON files under
`plugins/settings/TargetLines/instances` are no longer read and may be removed.

<details>
<summary><h2>Uninstallation</h2></summary>

Unload TargetLines:

```text
//lua unload TargetLines
```

Remove `Windower/addons/TargetLines` and delete its startup line. Optional v1
backups named `Windower/plugins/TargetLines.dll.v1-disabled*` and the unused
`Windower/plugins/settings/TargetLines` directory may also be removed.

</details>

## Features

<img width="480" height="270" alt="TargetLines battle view" src="https://github.com/user-attachments/assets/eb95a8bf-bb16-4c48-893b-af8c96b5425d" />

- Regular attacks draw once per source-target pair by default.
- Spells, job abilities, weapon skills, monster TP moves, and pet or avatar
  actions draw as they happen.
- Player, party, trust, pet, enemy, and unrelated-party lines can be controlled
  independently.
- Area actions feature 3 selectable styles: Ring (A), Ring (B), Fan, or can be turned Off.
- Line state is sent directly from Lua to the native renderer in the current
  game process.
- Each multibox client therefore owns an independent render state without
  shared JSON files, file polling, or cross-client file contention.

<img width="480" height="270" alt="TargetLines rotating combat view" src="https://github.com/user-attachments/assets/eb3c25ad-8caf-4efb-a81c-1d76ea311f04" />

The addon handles packets, settings, action tracking, and entity selection.
Its addon-local `_TargetLines.dll` performs the native D3D8 rendering through
the shared SceneHook ABI, allowing compatible native drawing addons such as
TargetRing to coexist safely.

## Line colors

Default colors:

- Blue: player, party, trust, or pet actions against enemies.
- Green: friendly support actions within the party or trust group.
- Red: enemy actions targeting the player, party, trusts, or pets.
- Magenta: NPC-to-NPC actions, including enemy-to-enemy actions and unrelated
  players' trusts.

Color blind mode uses cyan or blue for player actions, yellow or gold for
friendly support, vermilion for enemy actions, and magenta or pink for
NPC-to-NPC actions.

## AoE indicators

Supported area actions can display their affected targets using one of four
styles.

<img width="720" height="405" alt="TargetLines Ring A indicator" src="https://github.com/user-attachments/assets/df6ef290-403e-48aa-adeb-0f6d849834e3" />

- **Ring (A):** expanding AoE ring with orbiting line target indicators.
- **Ring (B):** expanding AoE ring with contracting target indicators.
- **Fan:** fan out lines from the action center to each affected target.
- **Off:** no AoE indicator; ordinary target lines remain enabled.

Ring (A) is the default for new installations. Choose a style from `AoE Style`
in `//tl config` or use `//tl aoemode off|fan|ring1|ring2`.

https://github.com/user-attachments/assets/d91f9572-b2b1-411f-b94f-7ac4855fa7c4

## Settings

Open or close the settings panel with either command:

```text
//tl config
//tl settings
```

The panel provides controls for the master display toggle, source categories,
special actions, AoE style and opacity, color blind mode, line width, source
opacity, line duration, and regular-attack behavior. Individual category
values remain saved while the master `Enable Lines` toggle is off.

### Commands

```text
//tl on                          Enable line output.
//tl off                         Disable line output.
//tl config                      Open or close the settings panel.
//tl settings                    Open or close the settings panel.
//tl playerlines [on|off]        Toggle player-origin lines.
//tl partylines [on|off]         Toggle party/trust-origin lines.
//tl petlines [on|off]           Toggle allied pet-origin lines.
//tl enemylines [on|off]         Toggle enemy-origin lines.
//tl otherpartylines [on|off]    Toggle unrelated-party lines.
//tl speciallines [on|off]       Toggle abilities, spells, WS, and TP moves.
//tl aoemode off|fan|ring1|ring2 Select the AoE presentation.
//tl aoeopacity <0.1-1.25>|+|-  Adjust AoE opacity.
//tl colorblind [on|off]         Toggle color blind mode.
//tl regular first|repeat|off    Configure regular-attack lines.
//tl playeropacity +|-           Adjust player line opacity.
//tl allyopacity +|-             Adjust ally line opacity.
//tl enemyopacity +|-            Adjust enemy line opacity.
//tl width +|-                   Adjust line width.
//tl fade +|-                    Adjust line duration.
//tl clear                       Clear active lines and attack memory.
//tl status                      Print addon and native-renderer status.
```

Aliases are `//targetlines` and `//tl`.

<details>
<summary><h3>Advanced and diagnostic commands</h3></summary>

```text
//tl opacity +|-                 Adjust the global opacity multiplier.
//tl glow +|-                    Adjust line glow.
//tl sourceheight +|-            Adjust the source anchor height.
//tl targetheight +|-            Adjust the target anchor height.
//tl timeout <seconds>           Set line duration directly.
//tl range <yalms>               Set the nearby scan range.
//tl interval <seconds>          Set the native-state update interval.
//tl cooldown <seconds>          Set the regular-attack repeat delay.
//tl specialcooldown <seconds>   Set the special-action repeat delay.
//tl claim [on|off]              Toggle experimental claim fallback lines.
//tl autoinspect [on|off]        Toggle automatic inspect snapshots.
//tl autoinspect interval <sec>  Set the inspect interval; minimum 30 seconds.
//tl actiondebug [on|off]        Toggle action-packet diagnostic logging.
//tl boneprobe                   Probe the anchor used by the latest line.
//tl inspect                     Write a model and anchor snapshot.
//tl nativestatus                Print detailed native lifecycle status.
//tl nativestart                 Start the native renderer manually.
//tl nativestop                  Stop the native renderer manually.
```

`//tl claim on` is disabled by default because inferred claim lines can be
noisy. `//tl autoinspect on` writes diagnostic model and anchor snapshots; it
does not change behavior automatically.

The current automatic anchor mapping uses the generally suitable bone 21 and
bone 39 for Mithra NPCs, trusts, and verified trust-model exceptions. Some
position differences may be caused by model animation or weapon stance rather
than skeleton identity, so unusual models may require further investigation.

Legacy v1 native probe commands such as `luamobprobe` and `dynamicbone` are not
available through the v2 Lua-loaded module.

</details>

## Local data and logs

TargetLines creates local files under its addon folder as needed:

- `data/settings.xml` stores saved settings.
- `runtime.log` records bounded addon diagnostics and rotates at 5 MiB to
  `runtime.log.1`.
- `native.log` records bounded native-renderer diagnostics and rotates at
  5 MiB to `native.log.1`.
- `inspect.log` is written only by manual or enabled automatic inspection.

These generated files are ignored by Git and are not included in release
packages. V2 does not create per-character JSON render-state files. Render
snapshots are passed directly to the native module and disappear with the game
process.

## Troubleshooting

- Run `//tl nativestatus` to inspect module, SceneHook, device, client, and
  state-binding status.
- If `_TargetLines.dll` is missing or incompatible, TargetLines fails closed:
  packet handling may remain loaded, but native rendering stays disabled.
- Reinstall the complete `addons/TargetLines` folder if the module is missing
  or was quarantined by antivirus software.
- Remove `load targetlines` from startup scripts if an upgrade remains blocked
  waiting for the legacy plugin.
- Do not restore a v1 plugin while the v2 addon is loaded; two renderers must
  not run together.

## Development

```text
addons/TargetLines/          Installable Windower addon.
src/native/                  Supported v2 Lua-native renderer source.
src/native/SceneHook/        Vendored SceneHook ABI, documentation, and license.
src/plugin/                  Archived v1 plugin source; not part of v2 runtime.
```

See `src/native/README.md` for the 32-bit build command. The release DLL should
export only `luaopen__TargetLines`.

### Release packaging

The [`Build TargetLines release`](.github/workflows/release.yml) GitHub Actions
workflow performs the strict 32-bit build, validates the DLL architecture,
export table, runtime imports, and package contents, then creates a ZIP
containing one top-level `TargetLines` folder. A manual workflow run stores the
ZIP as an Actions artifact. Publishing a GitHub release also attaches the ZIP
directly to that release.

The release archive intentionally contains only:

```text
TargetLines/
  TargetLines.lua
  LICENSE.md
  THIRD_PARTY_NOTICES.md
  libs/
    _TargetLines.dll
```

## License

TargetLines code authored by MogSafe is distributed under the MIT License. See
the repository's `LICENSE` file or the `LICENSE.md` included in the installable
addon folder. Vendored third-party components retain their own licenses;
SceneHook is distributed under Broguypal's BSD 3-Clause License as documented
in `addons/TargetLines/THIRD_PARTY_NOTICES.md`.

## Acknowledgements

TargetLines is maintained and authored by MogSafe. It includes SceneHook ABI
v2 and adapts renderer device-discovery work from TargetRing, authored by
Broguypal and distributed under the BSD 3-Clause License. See
`THIRD_PARTY_NOTICES.md` in the addon package.

TargetLines is inspired by Final Fantasy XII's target-line battle UI and prior
FFXI target-line addon concepts, including
[Jyouya/targetlines](https://github.com/Jyouya/targetlines) and its forks. No
source code or assets from those projects are included.
