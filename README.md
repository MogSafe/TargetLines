# TargetLines

<img width="640" height="360" alt="fight_short_0" src="https://github.com/user-attachments/assets/96355259-5cf1-4f55-94dc-ba106f6d363c" />

TargetLines is a Windower package that draws FFXII-style target lines during combat.
Lines indicate action flow between players, party members, trusts, pets, and enemies.


It includes a Lua addon for packet handling and settings, plus a native plugin
that draws lines on screen in 3D space.

<img width="480" height="270" alt="fight_short1_small" src="https://github.com/user-attachments/assets/eb95a8bf-bb16-4c48-893b-af8c96b5425d" />


Maintainer: [MogSafe](https://github.com/MogSafe)

## Installation

### Git

Clone the repository somewhere convenient, then copy or link the addon and plugin
folders into your Windower folder:

```powershell
cd path\to\Windower\repos
git clone https://github.com/MogSafe/TargetLines.git
```

The final runtime files should look like this:

```text
Windower\addons\TargetLines\TargetLines.lua
Windower\plugins\TargetLines.dll
```

### ZIP Download

Download the repository ZIP from GitHub, extract it, and copy these folders into
your Windower folder:

```text
addons\TargetLines
plugins\TargetLines.dll
```

If the extracted folder is named something like `TargetLines-main`, copy the
contents from inside that folder rather than loading it directly.

## Repository Layout

```text
addons/TargetLines/        Runtime Lua addon. Handles packets, settings, and line state.
plugins/TargetLines.dll    Runtime native plugin. Draws lines in the 3D scene.
src/plugin/                Native plugin source. Only needed for development or rebuilds.
```

For normal installation, **COPY ONLY**:

- `addons/TargetLines`
- `plugins/TargetLines.dll`

### Loading

Load both parts in-game:

```text
//load targetlines
//lua load TargetLines
```

To load automatically, add these to your Windower startup commands, such as
`Windower\scripts\init.txt`, or to your profile:

```text
load targetlines
lua load TargetLines
```

<details>
<summary><h2>Uninstallation</h2></summary>

Unload both parts in-game:

```text
//lua unload TargetLines
//unload targetlines
```

Remove these runtime files from your Windower folder:

```text
Windower\addons\TargetLines
Windower\plugins\TargetLines.dll
```

If you added TargetLines to your Windower startup commands, such as
`Windower\scripts\init.txt`, or to your profile, remove these lines as well:

```text
load targetlines
lua load TargetLines
```

Optional local data can also be removed:

```text
Windower\plugins\settings\TargetLines
```

</details>

## Features

TargetLines watches action packets and writes recent source-to-target line
events to a server/character-specific runtime file, for example:

```text
plugins/settings/TargetLines/instances/Asura-MogSafe.json
```

Each game client reads only its own character file. This prevents concurrent
clients using the same Windower installation from replacing one another's line
state while multiboxing.

<img width="480" height="270" alt="TLW-fight1_rotate_small" src="https://github.com/user-attachments/assets/eb3c25ad-8caf-4efb-a81c-1d76ea311f04" />

The native plugin renders those lines with D3D8 (Direct3D 8) using the current
camera projection, so lines follow the 3D scene as the camera moves.


By default:

- Regular attacks draw once per source-target pair.
- Spells, job abilities, weapon skills, monster TP moves, and pet/avatar actions draw as they happen.
- Area actions use Ring (A); Fan, Ring (B), and Off remain selectable.



https://github.com/user-attachments/assets/d91f9572-b2b1-411f-b94f-7ac4855fa7c4



## Line Colors

Default colors:

- Blue: player, party, trust, or pet actions against enemies.
- Green: friendly support actions within your party/trust group.
- Red: enemy actions targeting the player, party, trusts, or pets.
- Magenta: NPC-to-NPC actions, such as enemy-to-enemy actions or other players' trusts.

Color blind mode uses:

- Cyan/blue: player actions.
- Yellow/gold: friendly support actions.
- Vermilion: enemy actions.
- Magenta/pink: NPC-to-NPC/enemy-to-enemy actions.


 ## AoE Indicators

Area of effect visualization for affected targets of supported AoE spells,
abilities, and monster actions.

<img width="720" height="405" alt="aoe_ring_a_ally" src="https://github.com/user-attachments/assets/df6ef290-403e-48aa-adeb-0f6d849834e3" />

Styles can be chosen from `AoE Style` in `//tl config`:

- **Ring (A)** — expanding AoE ring with orbiting line target indicators.
- **Ring (B)** — expanding AoE footprint with contracting ring target indicators.
- **Fan** — fan-out lines drawn from the action center to each affected target.
- **Off** — disables AoE indicators without disabling regular target lines.

*Ring (A)* is selected by default on new installations.


## Settings

Open or close the settings panel:

```text
//tl settings
//tl config
```

The settings panel includes:

> - `Enable Lines` _// master on/off switch_
> - `Player Lines` _// player-origin action lines_
> - `Party/Trust Lines` _// party and trust-origin action lines_
> - `Pet Lines` _// allied pet-origin action lines_
> - `Enemy Lines` _// enemy-origin action lines_
> - `Other Party Lines` _// lines unrelated to your party_
> - `Abilities/Spells` _// spells, weapon skills, abilities, and TP moves_
> - `AoE Style` _// fan-out lines, Ring (A) orbiting line markers, Ring (B) contracting markers, or off_
> - `Color Blind Mode` _// alternate color palette_
> - `Line Width` _// line thickness preset_
> - `Player Opacity` _// opacity for player-origin lines_
> - `Ally Opacity` _// opacity for party, trust, and pet lines_
> - `Enemy Opacity` _// opacity for enemy-origin lines_
> - `Line Duration` _// how long action lines remain visible_
> - `AoE Opacity` _// opacity for the selected area-action indicator style_
> - `Regular Attacks` _// first hit, delayed repeats, or off_

`Enable Lines` is the master display toggle. The individual line toggles keep
their saved values while the master toggle is off.

<details>
<summary><h2>Commands</h2></summary>

Most common options are available in `//tl config`. This section also lists
additional commands, including debug commands.

```text
//tl on                         Enable line output.
//tl off                        Disable line output.
//tl config                     Open or close the settings panel.
//tl settings                   Open or close the settings panel.
//tl playerlines [on|off]       Toggle player-origin lines.
//tl partylines [on|off]        Toggle party/trust-origin lines.
//tl petlines [on|off]          Toggle allied pet-origin lines.
//tl enemylines [on|off]        Toggle enemy-origin lines.
//tl otherpartylines [on|off]   Toggle lines unrelated to your party/trusts/pets.
//tl speciallines [on|off]      Toggle abilities/spells/weapon skills/TP moves.
//tl aoemode off|fan|ring1|ring2 Select the AoE indicator presentation.
//tl aoeopacity <0.1-1.25>|+|- Adjust opacity for the selected AoE style.
//tl colorblind [on|off]        Toggle color blind mode.
//tl regular first|repeat|off   Configure regular attack lines.
//tl playeropacity +|-          Adjust player line opacity.
//tl allyopacity +|-            Adjust ally line opacity.
//tl enemyopacity +|-           Adjust enemy line opacity.
//tl width +|-                  Adjust line width.
//tl fade +|-                   Adjust line duration.
//tl clear                      Clear active lines and first-attack memory.
//tl status                     Print status and write status to runtime log.
//tl inspect                    Write an inspect snapshot to inspect.log.
//tl autoinspect [on|off]       Periodically write inspect snapshots when enemies are nearby.
```

### Advanced/debug commands

```text
//tl opacity +|-                Adjust global opacity multiplier.
//tl glow +|-                   Adjust line glow.
//tl sourceheight +|-           Adjust source anchor height.
//tl targetheight +|-           Adjust target anchor height.
//tl timeout <seconds>          Set line duration directly.
//tl range <yalms>              Set nearby scan range.
//tl interval <seconds>         Set state write interval.
//tl cooldown <seconds>         Set regular attack repeat delay.
//tl specialcooldown <seconds>  Set special action repeat delay.
//tl claim [on|off]             Toggle claim-based fallback lines.
//tl autoinspect interval <sec> Set auto inspect interval, minimum 30 seconds.
//tl fanlines [on|off]          Legacy compatibility toggle; prefer aoemode.
//tl actiondebug [on|off]       Toggle action packet debug logging.
//tl boneprobe                  Request native anchor probe for latest line.
//tl luamobprobe                Run the native LuaCore mob/bone diagnostic probe.
//tl dynamicbone <auto|off|0-255> Control the native dynamic bone anchor; auto uses bone 21, with bone 39 for Mithra NPCs/trusts and verified trust-model exceptions.
```

The bone 39 exceptions are provisional. Their positional differences may be
caused by model animation or weapon stance rather than skeleton identity alone,
so the automatic anchor mapping may need further investigation as more models
and animations are tested.

`//tl claim on` enables an experimental fallback for missed actions. It infers
temporary lines from nearby claimed enemies instead of action packets, and is
disabled by default because it can be noisy.

`//tl autoinspect on` writes inspect snapshots when enemies are nearby, first
after zoning and then periodically. Snapshots record model and anchor data for
review; they do not change behavior automatically.

</details>

Aliases:

```text
//targetlines
//tl
```

## Acknowledgements

TargetLines is inspired by Final Fantasy XII's targetline battle UI and by
prior FFXI targetline addon concepts, including
[Jyouya/targetlines](https://github.com/Jyouya/targetlines) and its forks. No
source code or assets from those projects are included.

References:

- https://github.com/Jyouya/targetlines
- https://github.com/LuckyCharms2020/targetlines

<details>
<summary><h2>Local data</h2></summary>

TargetLines creates local user-specific files as needed:

- `addons/TargetLines/data/settings.xml` stores saved addon settings.
- `plugins/settings/TargetLines/instances/<server>-<character>.json` is the
  transient runtime state file used by the addon and plugin. One file is used
  per server/character so multiple game clients remain isolated.

These files are generated locally and are intentionally ignored by Git.

The active character file is cleared before the native plugin binds to it and
is normally removed on logout or addon unload. A file left behind by a crash is
safe: the next login overwrites it with empty state before rendering begins.

Settings from earlier releases migrate automatically. `aoe_mode` and
`aoe_opacity_scale` are the authoritative v1.1 settings. The legacy XML keys
`show_fan_lines`, `fan_opacity_scale`, and `ring_indicator_style` are retained
only for migration compatibility and should not be edited directly.

</details>
