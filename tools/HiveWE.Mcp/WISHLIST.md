# MCP tool wishlist

Tools and improvements that would have helped while building maps through the MCP server (first collected while building "Defence of Uppsala").

## Saving

- **A dedicated `hivewe_save` tool.** Saving exists today as `hivewe_map` with `action: "save"`, which is easy to miss. A top-level save tool, and an optional autosave every N minutes or N edits, would protect long build sessions.

## Discovering content

- **Search across object kinds.** Trees are destructibles, not doodads, so `hivewe_objects` with `kind: "doodad"` and `search: "tree"` finds almost nothing. A search over all kinds at once would avoid guessing the kind.
- **Model availability per doodad variation.** Some doodads have variations without a model in the current graphics mode (e.g. `LOlp` variations 3-5, `VSvb`, `VTlt`, `YOac`, `LObs`). They show as green boxes and could look broken in game. A call that lists which variations of a type have models would let tools avoid them.

## Terrain

- **Path strokes.** Paint, raise, flatten or add water along a polyline with a given width in one call, instead of hundreds of single brush strokes. Rivers and roads are the most common case.
- **Explicit water level.** `terrain.water` takes its height from the ground under the first stroke and spreads it, which made rivers come out too deep. A `water_height` parameter would make shallow rivers straightforward.
- **Scatter.** Fill an area or band with random doodads at a density, with exclusion zones (roads, water, plazas), and random variation, scale and facing. Forests took a custom script.

## Seeing the map

- **Offscreen screenshots.** Screenshots fail while the HiveWE window is minimized. Rendering to an offscreen target would make them always work.
- **View toggles.** Turn click helpers (the green boxes on small doodads), pathing, regions and the brush on or off for screenshots, through the API.
- **Whole-map overview.** A top-down image of the whole map with doodads and units drawn, not just terrain like `hivewe_minimap`.

## Scripts and testing

- **Lua syntax checking in `script.build`.** JASS goes through JassHelper, but Lua isn't checked at all, so syntax errors only surface in game.
- **Game log readback.** After `hivewe_map test`, read Warcraft III's log and Lua errors, so script problems can be fixed without asking the player.
