# ReforgerOpforPlayerCommander

Arma Reforger mod that lets a player run the OPFOR side of a coop mission from the Game Master
editor **without** the omniscient view a normal Game Master has.

## Fog of War for the Game Master

When enabled (toolbar button next to *Hide interface*), the **playable (player) faction** is
hidden from the local Game Master: its characters, vehicles, groups and players are only shown
while **AI hostile to that faction** (the OPFOR the commander is running) currently have them
spotted (any detection level). Clicking the button cycles
`OFF → playable faction 1 → playable faction 2 → … → OFF`; the tooltip shows which faction is
being hidden. Everything else (the commander's own AI, props) is always visible. Spotted enemies stay revealed for a configurable timeout (default 30 s) after the
last AI contact.

Hidden entities are removed on the client only:

- no editor icons / labels, cannot be hovered, selected, inspected or acted on
- 3D model is not rendered in the editor camera
- unit icon hidden on the 2D map

The toggle is purely local; the server only decides *what* is spotted.

### Configuration

Set on the game mode prefab (`SCR_BaseGameMode` → category **OPC Fog of War**) or per scenario in
the mission header (`SCR_MissionHeader` → **OPC Fog of War**, overrides the game mode):

| Setting | Default | Meaning |
|---|---|---|
| Hidden Faction Key | `US` | Fallback faction to hide when the commander requests none / an unknown one (normally the toolbar picks a playable faction) |
| Reveal Timeout | `30` s | How long an enemy stays revealed after last contact |
| Update Interval | `1` s | Perception poll / network update rate |
| Players Can Spot | off | Also count player-controlled characters hostile to the hidden faction as spotters |

### How it works

| File | Role |
|---|---|
| `Scripts/Game/OPC/OPC_FogOfWarServer.c` | Server: polls `PerceptionComponent` of every AI hostile to the hidden faction, keeps last-seen times, sends `RplId` lists to subscribed commanders |
| `Scripts/Game/OPC/OPC_FogOfWarClient.c` | Client: enemy/revealed predicate, hides/unhides entities (editor VISIBLE state, `EntityFlags.VISIBLE`, map item) |
| `Scripts/Game/OPC/OPC_FogOfWarToolbarAction.c` | The toolbar toggle button |
| `Scripts/Game/OPC/Modded/SCR_ToolbarActionsEditorComponent.c` | Inserts the button after *Hide interface* (script-only, no prefab override → no clash with GME) |
| `Scripts/Game/OPC/Modded/SCR_EditableEntityFilters.c` | Denies VISIBLE / HOVER editor states to hidden entities |
| `Scripts/Game/OPC/Modded/SCR_PlayerController.c` | RPCs: subscribe (client→server), config + spotted list (server→owner) |
| `Scripts/Game/OPC/Modded/SCR_BaseGameMode.c` | Owns the server logic, exposes settings |
| `Scripts/Game/OPC/Modded/SCR_MissionHeader.c` | Per-scenario overrides |

Works with or without Game Master Enhanced (no dependency).

### Known limitations / notes

- Empty vehicles with no faction affiliation are never hidden (there is no faction to compare).
- Fog of war is suspended while the editor is closed (so a GM who jumps into a soldier isn't blinded) and re-applied when it is opened again.
- Entities that stream in while hidden are evaluated ~250 ms after they register.
- Icons are vanilla placeholders — swap `ICON_OFF` / `ICON_ON` in `OPC_FogOfWarToolbarAction.c` for custom textures.
