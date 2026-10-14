# MCP tool wishlist

Ideas for tools that would help when building maps through the MCP server. Add new ones under **Open**.

## Open

Nothing yet.

## Done

All of these came out of building "Defence of Uppsala":

| Wish | Now |
|---|---|
| A dedicated save tool, and autosave | `hivewe_save` (with `autosave_minutes`), editor methods `map.save` and `map.autosave` |
| Search across object kinds | `hivewe_objects` action `search` (`objects.search`) |
| Which doodad variations have models | `hivewe_doodads` action `models`; created doodads pick only random variations that have models |
| Path strokes | `hivewe_terrain` action `path`: any brush action along a polyline, one undo step |
| Explicit water level | `height` on `hivewe_terrain` action `water` |
| Painting large areas by rules | `hivewe_terrain` action `paint_grid`: every corner's tile in one call |
| Scatter | `hivewe_doodads` action `scatter`: random placement with density, spacing, exclusion zones, scale and facing |
| Offscreen screenshots | `hivewe_screenshot` renders offscreen, so it works while the editor is minimized |
| View toggles | `hivewe_view`: click helpers, pathing, regions and the other overlays |
| Whole-map overview | `hivewe_overview` |
| Lua syntax checking | `hivewe_script` action `build` parses the generated Lua; `set_header` rejects Lua with syntax errors |
| Game log readback | `hivewe_game_log` |
