# Opfor Player Commander

An Arma Reforger mod that lets a player run the OPFOR side of a coop mission from the Game
Master editor without the omniscient view a normal Game Master has. With fog of war enabled,
the commander only sees the player faction where their own AI can see it — so their decisions
come from reports and contacts, not from an all-seeing camera.

Works on dedicated servers, listen servers, and in Workbench. No dependencies; compatible with
Game Master Enhanced (GME).

## How it works for the commander

A **Fog of War** button sits at the right-hand end of the Game Master toolbar. Clicking it
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

## Contact reports

When AI first spot something, the commander gets an entry in the Game Master notification log —
**Contact: Alpha 1-1** — carrying the position it was seen at.

- Click the entry to jump the editor camera there.
- Or press the **go to last notification** key (vanilla keybind, `EditorLastNotificationTeleport`)
  to jump straight to the newest one. No new keybind is added.

Infantry collapses to its group, so a spotted squad is one report rather than eight. Vehicles always
report on their own, because armour turning up is its own piece of news. The same subject won't
report again for 2 minutes (configurable, `0` turns reports off).

The position in a report is **where the unit was seen, not where it is now**. Jumping to a two
minute old contact puts the camera over the spot it was reported from — which may well be empty by
the time you get there. That is the point; a report that tracked the unit would be a live feed of
something that has since gone back into the fog.

Reports are raised locally on the commander's machine and only while fog of war is on. Reopening the
editor mid-firefight does not dump the whole front line into the log — everything already in contact
at that moment is treated as known.

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
(`SCR_MissionHeader` → **OPC Fog of War**), which overrides the game mode. Every setting can be
overridden from the header; leave a header field at its neutral value to fall through to the
game mode.

| Setting | Default | Header neutral value | Description |
|---|---|---|---|
| `Hidden Faction Key` | `US` | empty | Fallback faction to hide when the commander's request is empty or unknown. The toolbar normally picks a playable faction, so most missions never use this. |
| `Reveal Timeout` | `30` | `0` | Seconds a spotted entity stays revealed after the last AI contact. |
| `Update Interval` | `1` | `0` | Seconds between perception polls. Updates are only sent to the commander when the spotted set actually changes. |
| `Players Can Spot` | off | `-1` | When enabled, player-controlled characters hostile to the hidden faction also reveal what they perceive. The header field is a tri-state: `-1` game mode default, `0` off, `1` on. |
| `Contact Report Cooldown` | `120` | `-1` | Seconds before the same contact can raise another report. `0` disables contact reports entirely. |

## Custom icon

The mod ships a matching pair of toolbar icons in `UI/Textures/OPC/`
(`OPC_FogOfWar_Off.png` / `OPC_FogOfWar_On.png`, with the `.svg` sources beside them), but it
loads vanilla textures by default so that it works without any asset pipeline step.

To use the custom pair:

1. Open the addon in Workbench and let the Resource Browser import the two PNGs. Workbench
   generates the `.edds` and assigns each one a GUID.
2. Right-click each imported texture → **Copy Resource Name**.
3. Paste the two resource names over `ICON_OFF` and `ICON_ON` in
   `Scripts/Game/OPC/OPC_FogOfWarToolbarAction.c` (the file has the target lines commented out
   directly above).

## Architecture

The server decides what is spotted; the client decides what to draw.

| File | Role |
|---|---|
| `Scripts/Game/OPC/OPC_FogOfWarServer.c` | Server. One pass per tick over every AI hostile to a hidden faction, bucketing contacts into each subscribed faction's sighting set, applying the reveal timeout, and sending the spotted entities (as `RplId` lists) to commanders whose view is out of date. |
| `Scripts/Game/OPC/OPC_FogOfWarClient.c` | Client. Decides which editable entities to hide, and hides them: denies the editor `VISIBLE` state, clears `EntityFlags.VISIBLE` for the 3D camera, and hides the map descriptor item. Owned by the local player controller. |
| `Scripts/Game/OPC/OPC_FogOfWarToolbarAction.c` | The toolbar button (a `SCR_BaseToggleToolbarAction`). |
| `Scripts/Game/OPC/OPC_ContactReports.c` | Contact reports. Adds one `ENotification` value, its display data, and the client-side reporter that collapses contacts to groups and applies the per-subject cooldown. |
| `Scripts/Game/OPC/Modded/SCR_ToolbarActionsEditorComponent.c` | Appends the button to the Game Master toolbar from script. No prefab or config overrides, so it doesn't clash with GME or other editor mods. |
| `Scripts/Game/OPC/Modded/SCR_EditableEntityFilters.c` | Denies the `VISIBLE` and `HOVER` editor states to hidden entities. |
| `Scripts/Game/OPC/Modded/SCR_PlayerController.c` | Network bridge: subscribe (client → server), config and spotted list (server → owner). |
| `Scripts/Game/OPC/Modded/SCR_BaseGameMode.c` | Owns the server logic and exposes the settings. |
| `Scripts/Game/OPC/Modded/SCR_MissionHeader.c` | Per-scenario setting overrides. |

### Why the button is at the end of the toolbar

Server-performed toolbar actions travel over the network as an *index* into the editor's sorted
action list, so client and server have to agree on that list. Inserting the button next to
**Hide interface** would shift every vanilla index after it, which is only safe as long as no
other mod also inserts into that list from script — client and server reach the list through
different entry points, so two lazily inserting mods can interleave differently on the two sides
and the server then performs the wrong action. Appending leaves every vanilla index untouched.

## Limitations

- Hiding is client-side. It removes information from the commander's screen but is not a
  server-enforced anti-cheat; it assumes a cooperative commander.
- Allies of the hidden faction are hidden with it. Factions neutral to it stay visible.
- Entities without a faction (empty vehicles that never had a crew, props) are never hidden.
- The commander's own faction choice in the respawn menu doesn't matter; the button cycles
  through playable factions directly.
- UI strings are English only.

## License

See [LICENSE](LICENSE).
