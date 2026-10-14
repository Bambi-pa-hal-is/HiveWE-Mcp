# HiveWE.Mcp

An MCP server that lets Claude build Warcraft III maps in the running HiveWE editor. It forwards MCP tool calls to the editor's JSON-RPC API (`src/api/` in HiveWE), which applies them live on the editor's main thread.

```
Claude ──MCP (HTTP)──> HiveWE.Mcp ──JSON-RPC over HTTP──> HiveWE (127.0.0.1:47777/rpc)
```

## Setup

1. Build the server once (needs the .NET 10 SDK): `dotnet build -c Release`
2. Start `HiveWE.exe`. It starts this server on `http://127.0.0.1:47778/mcp` and stops it when the editor closes.
3. Register it once in Claude Code:
   ```shell
   claude mcp add --transport http hivewe http://127.0.0.1:47778/mcp
   ```

The **MCP** tab in HiveWE's ribbon has a **Connect Claude** button with:
- the command above, ready to copy
- the API and server status, and the server's output
- Start/Restart, Stop and Browse (to pick another `HiveWE.Mcp.exe`)
- a switch to stop starting the server with HiveWE

HiveWE looks for the server in this order:
1. the path chosen with Browse
2. `mcp\HiveWE.Mcp.exe` next to `HiveWE.exe`
3. `tools\HiveWE.Mcp\bin\Release\net10.0\HiveWE.Mcp.exe` in a source checkout

### Other ways to run it

- **stdio:** an MCP client starts the server itself, e.g. `claude mcp add hivewe -- "<path>\HiveWE.Mcp.exe"`. Start HiveWE with `--no-mcp` so it doesn't also start one.
- **Ports:** `HiveWE.exe --api-port <port>` moves the editor API, and `--no-api` turns it off. The server takes `--http`, `--port <port>` and `--api-url <url>`; without `--api-url` it falls back to the `HIVEWE_API_URL` environment variable.

## Tools

Ideas for more tools go in [WISHLIST.md](WISHLIST.md).

| Tool | What it does |
|---|---|
| `hivewe_status` | Editor version, Warcraft III directory, graphics mode |
| `hivewe_map` | info, new, open (folder or .w3x), save, export (.w3x), test in Warcraft III |
| `hivewe_save` | Save, save as, and autosave |
| `hivewe_screenshot`, `hivewe_overview`, `hivewe_minimap` | Images: the map view (rendered offscreen), the whole map from above, the terrain minimap |
| `hivewe_view` | Toggle click helpers, pathing, regions and other overlays |
| `hivewe_game_log` | The end of Warcraft III's log files after a test |
| `hivewe_camera` | Editor camera |
| `hivewe_undo` | Undo/redo map edits |
| `hivewe_terrain` | Raise/lower, smooth, flatten, paint tiles, cliffs and ramps, water (optionally at an exact height), boundaries, strokes along a path, painting a whole grid of tiles, tile list and tileset, reading corners |
| `hivewe_pathing` | Pathing cells |
| `hivewe_doodads`, `hivewe_units`, `hivewe_items` | Placed objects: list, create (many at once), update, delete; doodads also scatter randomly and report which variations have models |
| `hivewe_regions`, `hivewe_game_cameras` | Regions and camera setups |
| `hivewe_objects` | Object editor data for all seven kinds: list, search across kinds, fields, create custom objects, set/reset fields, delete |
| `hivewe_gameplay_constants` | Gameplay constants |
| `hivewe_map_info`, `hivewe_players`, `hivewe_forces`, `hivewe_tech`, `hivewe_random_tables` | Scenario settings |
| `hivewe_script`, `hivewe_triggers`, `hivewe_variables`, `hivewe_strings` | JASS/Lua map header, script triggers, variables, trigger strings, compiling |
| `hivewe_build_typescript` | Runs a TypeScript-to-Lua (w3ts) build and loads the bundle into the map header |
| `hivewe_imports`, `hivewe_sounds` | Imported files and sound definitions |
| `hivewe_batch` | Many calls in one request, undone as one step |
| `hivewe_call` | Any editor API method directly |

The shared conventions are sent to the client as the server instructions (`Instructions.cs`): world coordinates in game units, sizes in tiles, objects identified by creation number.

## Calling the editor API directly

The API is plain JSON-RPC 2.0. Requests must use `Content-Type: application/json`. Method names are `<tool>.<action>`, e.g. `doodads.create`.

```shell
curl -s http://127.0.0.1:47777/rpc -H "Content-Type: application/json" -d '{"jsonrpc":"2.0","id":1,"method":"map.info"}'
```

Error codes: the standard JSON-RPC ones, plus:
- `-32001`: timed out waiting for the editor, e.g. a modal dialog is open
- `-32002`: no map loaded
- `-32003`: the editor is shutting down
