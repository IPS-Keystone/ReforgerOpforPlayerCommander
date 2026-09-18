# Opfor Player Commander

An Arma Reforger mod that lets a player run the OPFOR side of a coop mission from the Game
Master editor without the omniscient view a normal Game Master has. With fog of war enabled,
the commander only sees the player faction where their own AI can see it — so their decisions
come from reports and contacts, not from an all-seeing camera.

Works on dedicated servers, listen servers, and in Workbench. No dependencies; compatible with
Game Master Enhanced (GME).

## How it works for the commander

A **Fog of War** button sits in the Game Master toolbar, next to **Hide interface**. Clicking it
cycles through the mission's playable factions and then off:

```
OFF → hide playable faction 1 → hide playable faction 2 → … → OFF
```

The tooltip names the faction being hidden, for example **Fog of War: hiding US Army**.

While a faction is hidden, its characters, vehicles, groups, and players disappear from the
commander's editor unless AI hostile to that faction currently have them spotted. Hidden
entities:

- have no editor icons or labels, and can't be hovered, selected, or inspected
- are not rendered in the 3D editor camera
- have no unit icon on the 2D map

A spotted entity stays revealed for 30 seconds (configurable) after the last AI contact, then
fades back into the fog. Spotting counts any detection level — a heard shot or a glimpsed
silhouette is enough. When AI spot a character inside a vehicle, the vehicle is revealed too,
and a spotted vehicle reveals its occupants.

The toggle only affects the commander's own screen. Other Game Masters and players are
unaffected, and closing the editor suspends the effect so a commander who possesses a soldier
sees the world normally.

## Installation

1. Subscribe to the mod in the Arma Reforger Workshop, or copy the `PlayerOpforCommander`
   addon folder into your addons directory.
2. Add the mod to your server's mod list. It must be loaded on both server and clients — the
   server computes what the AI can see, and the client does the hiding.

No scenario changes are required.

## Configuration

All settings are optional. Set them on the game mode entity (`SCR_BaseGameMode` → category
**OPC Fog of War**) in your scenario, or per scenario in the mission header
(`SCR_MissionHeader` → **OPC Fog of War**), which overrides the game mode.

| Setting | Default | Description |
|---|---|---|
| `Hidden Faction Key` | `US` | Fallback faction to hide when the commander's request is empty or unknown. The toolbar normally picks a playable faction, so most missions never use this. |
| `Reveal Timeout` | `30` | Seconds a spotted entity stays revealed after the last AI contact. |
| `Update Interval` | `1` | Seconds between perception polls and updates sent to the commander. |
| `Players Can Spot` | off | When enabled, player-controlled characters hostile to the hidden faction also reveal what they perceive. |

## Architecture

The server decides what is spotted; the client decides what to draw.

| File | Role |
|---|---|
| `Scripts/Game/OPC/OPC_FogOfWarServer.c` | Server. Polls the `PerceptionComponent` of every AI hostile to the hidden faction, tracks last-seen times, applies the reveal timeout, and sends the spotted entities (as `RplId` lists) to subscribed commanders. |
| `Scripts/Game/OPC/OPC_FogOfWarClient.c` | Client. Decides which editable entities to hide, and hides them: denies the editor `VISIBLE` state, clears `EntityFlags.VISIBLE` for the 3D camera, and hides the map descriptor item. Owned by the local player controller. |
| `Scripts/Game/OPC/OPC_FogOfWarToolbarAction.c` | The toolbar button (a `SCR_BaseToggleToolbarAction`). |
| `Scripts/Game/OPC/Modded/SCR_ToolbarActionsEditorComponent.c` | Inserts the button after **Hide interface** from script, keeping client and server action indices in sync. No prefab or config overrides, so it doesn't clash with GME or other editor mods. |
| `Scripts/Game/OPC/Modded/SCR_EditableEntityFilters.c` | Denies the `VISIBLE` and `HOVER` editor states to hidden entities. |
| `Scripts/Game/OPC/Modded/SCR_PlayerController.c` | Network bridge: subscribe (client → server), config and spotted list (server → owner). |
| `Scripts/Game/OPC/Modded/SCR_BaseGameMode.c` | Owns the server logic and exposes the settings. |
| `Scripts/Game/OPC/Modded/SCR_MissionHeader.c` | Per-scenario setting overrides. |

## Limitations

- Hiding is client-side. It removes information from the commander's screen but is not a
  server-enforced anti-cheat; it assumes a cooperative commander.
- Allies of the hidden faction are hidden with it. Factions neutral to it stay visible.
- Entities without a faction (empty vehicles that never had a crew, props) are never hidden.
- The commander's own faction choice in the respawn menu doesn't matter; the button cycles
  through playable factions directly.
- Button icons are vanilla placeholders. Replace `ICON_OFF` and `ICON_ON` in
  `OPC_FogOfWarToolbarAction.c` to use custom textures.
- UI strings are English only.

## License

See [LICENSE](LICENSE).
