# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

HiveWE is a Warcraft III World Editor written in C++23 with **C++20 modules** (`.ixx`), Qt6 for UI and Vulkan for rendering. Windows/MSVC is the primary target (Visual Studio 17.14+ is required for modules).

## Build and test

Dependencies come from vcpkg in manifest mode (`vcpkg.json`, with patched ports in `overlay-ports/`). `VCPKG_ROOT` must be set. The first configure builds all dependencies, including Qt, which takes a while.

```shell
cmake --preset Release            # or Debug / RelWithDebInfo / ReleaseTracing (Tracy profiling)
cmake --build --preset Release    # build presets: Debug, Release, RelWithDebInfoTracing
ctest --preset Release            # test presets: Debug, Release
```

- With only Visual Studio 2026 installed (no `cmake` on PATH, use VS's bundled one): the presets name the VS 2022 generator, and the VS 2026 compiler (14.5x) fails on this codebase's `#include`/`import` mixing. Build with the installed v143 (14.44) toolset instead, for both HiveWE and vcpkg: `cmake --preset Release -G "Visual Studio 18 2026" -T v143 -DVCPKG_OVERLAY_TRIPLETS=<absolute path to repo>/build/triplets`, where `build/triplets/x64-windows.cmake` is the stock triplet plus `set(VCPKG_PLATFORM_TOOLSET v143)`. VS's bundled vcpkg works as `VCPKG_ROOT`.
- Output goes to `build/<preset>/`; binaries land in `build/Release/Release/`. A post-build step symlinks `data/` next to the executable, which needs admin rights or Developer Mode on Windows.
- Tests use doctest (`tests/`, target `HiveWE_tests`, linked against `HiveWE_lib`). Run a single test case or filter with doctest flags on the test binary, e.g. `build/Release/Release/HiveWE_tests.exe -tc="*mdl*"`, or `ctest --preset Release -R <name>`. Fixtures live in `tests/fixtures/{mdl,mdx,slk}` and are exposed through the `MDL_FIXTURES_DIR`, `MDX_FIXTURES_DIR` and `SLK_FIXTURES_DIR` defines. New test files must be added to `tests/CMakeLists.txt`.
- GLSL shaders in `data/shaders/*.{vert,frag,comp}` are compiled to `.spv` **next to their sources** by glslang (a dependency of `HiveWE_lib`). Editing any `*.glsl` include rebuilds every shader.
- GCC and Clang builds use `-Werror`.
- CI (`.github/workflows`) builds the Release preset on windows-latest and runs ctest.

## Running

The app needs a Warcraft III install: game data is read from CASC, and the directory is asked for once and stored in QSettings. Startup flags in `src/main.cpp`:
- `--map <folder>` opens a folder map. The default is `data/test map/`.
- `--camera x,y[,distance]` or `--camera cliffs` positions the camera.
- `--quit-after <seconds>` exits after running unattended.
- `--vulkan-model-grid` and `--vulkan-model-editor` open standalone render test modes (`src/render/vulkan_test_modes.cpp`) instead of the editor.
- `--api-port <port>` moves the automation API (default 47777), and `--no-api` turns it off. `--no-mcp` keeps the MCP server from starting.

## Architecture

- **Two targets:** `HiveWE_lib` (static library holding nearly all code) and the `HiveWE` executable (`main.cpp`). Module interface files must be listed in the `FILE_SET ... CXX_MODULES` in `src/CMakeLists.txt`; plain `.cpp`/`.h` files (Qt widgets with `.ui` and moc) are listed separately. Module code does `import std;`, and headers that can't be imported go in the global module fragment (`module;` ... `export module X;`).
- **Global singletons** are exported `inline` variables in modules:
  - `map` (`MapGlobal`, `Map*`): the currently loaded map.
  - `hierarchy` (`Hierarchy`): the virtual file system.
  - `resource_manager`.
  - `camera`.
  - The game SLK and INI tables (`Globals`: `units_slk`, `abilities_slk`, ...).
- **File resolution (`src/base/hierarchy.ixx`):** `hierarchy.open_file()` looks for a file in this order: `data/overrides` → map imports → local files (when the registry allows them) → the CASC archive. It handles the SD, HD (Reforged, `_hd.w3mod`), Definitive (`_de.w3mod`) and teen asset variants plus `filealiases.json`. The graphics mode comes from QSettings: `graphics` = `SD`/`HD`/`DE`, plus `flavour` and `teen`.
- **ResourceManager (`src/base/resource_manager.ixx`):** a thread-safe cache keyed by path + `T::name` + an optional identifier. Resource types inherit from `Resource` and declare a static `name`. `load<T>()` returns `std::expected<std::shared_ptr<T>, std::string>`. Concurrent loads of the same key construct the resource only once.
- **Map (`src/base/map/map.ixx`):** owns the world state (terrain, doodads, units, regions, pathing map, triggers, imports, map info), loads and saves the map's files, and records rendering. Brushes (`src/brush/`) and undo commands (`*_undo.ixx`, `WorldUndoManager`) get a `WorldEditContext` from `map->edit_context()` instead of using the `map` global; keep it that way in new edit code.
- **Rendering:** Vulkan through volk, VMA and vk-bootstrap (`src/render/`), using bindless descriptors and a frame pacer. `RenderManager` batches skinned MDX meshes (`src/resources/skinned_mesh*`). Terrain, water, cliffs and the particle emitters have their own resources/renderers. Dear ImGui is bridged into Qt via `src/qt_imgui/`. GLM is configured for a 0..1 depth range (`GLM_FORCE_DEPTH_ZERO_TO_ONE`).
- **File formats (`src/file_formats/`):** MPQ (StormLib), CASC (CascLib), BLP, SLK, INI, JSON, and MDX/MDL models (reader/writer, validator, optimizer). Most unit tests cover these.
- **Object editor:** Qt model/view. `src/models/list/*` and `src/models/tree/*` are per-object-type models on top of `table_model.ixx` and the SLK data.
- **Other UI:** `src/main_window/` (main window and ribbon, Qt Advanced Docking System), `src/menus/` (palettes and dialogs), `src/trigger_editor/` (JASS editor using QScintilla), `src/model_editor/`.

## Automation API and MCP server

This fork adds a JSON-RPC 2.0 API so an MCP server can drive the running editor. Keep the API code isolated so merging upstream HiveWE stays painless.

**Hooks in upstream files:**
- `api::start()` in `main.cpp`, `add_subdirectory(api)` in `src/CMakeLists.txt`, and `cpp-httplib` in `vcpkg.json`.
- `VulkanViewport::capture()` and `MapWidget::viewport`, for screenshots.
- `Map::show_dialogs` and `Map::save_errors`, so saving reports problems instead of opening message boxes.
- `GameCameras::save` and `Sounds::save`, which were empty stubs upstream.
- `TriggerStrings::all()`.
- `WorldUndoManager` merging, and undo now runs a group's actions in reverse.
- The `InitUpgrades`/`InitTechTree` generation in `map_script.cpp`.

**Code layout:**
- `src/api/http_transport.cpp`: a plain `.cpp` that owns httplib and serves `POST /rpc` on `127.0.0.1` only, requiring `Content-Type: application/json`. Exporting httplib types from a module crashes MSVC, so httplib stays out of the modules.
- `src/api/api_server.ixx` (`ApiServer`): JSON-RPC parsing, the method registry, and the built-in `batch` method. Every handler runs on the Qt main thread (queued to `qApp`, with a 60s timeout), so handlers may touch `map` and the UI.
- Method modules, each exporting a `std::vector<MethodEntry>` table that `src/api/api.cpp` registers:
  - `api_editor.ixx`: ping/version/quit, undo, camera, screenshots
  - `api_map.ixx`: map lifecycle
  - `api_terrain.ixx`: terrain and pathing
  - `api_objects.ixx`: doodads, units, items, regions, game cameras
  - `api_objectdata.ixx`: object editor data, gameplay constants
  - `api_mapinfo.ixx`: map info, players, forces, tech, random tables
  - `api_script.ixx`: script header, triggers, variables, strings, building
  - `api_assets.ixx`: imports, sounds, export
- Methods are `nlohmann::json f(const nlohmann::json& params)`. Throw `api::Error` (`ApiError` module) for JSON-RPC errors; `required<T>(params, name)` in `ApiCommon` reports missing parameters.
- **Helpers for the main window:** `ApiHost` holds callbacks into the main window (load map, title, viewport capture, closing the trigger editor), filled in by `api.cpp`.
- **The MCP launcher:** `mcp_panel.cpp` adds the ribbon's MCP tab and starts `HiveWE.Mcp.exe --http` at launch.

**Conventions the methods follow:**
- The API speaks world coordinates (game units). Convert with `world_to_internal`/`internal_to_world`, since HiveWE stores positions in tiles from the bottom left corner.
- Placed objects are identified by creation number.
- Map edits go through `WorldCommand`s and `commit()` so they undo.
- Terrain edits drive an API-owned `TerrainBrush`, so cliffs, ramps and pathing match the editor.
- Object data goes through the global `TableModel`s so open editors and placed units update.

**Adding a method:**
1. Add the function and its table entry in a method module.
2. Add it to the matching tool's action list and description in `tools/HiveWE.Mcp/Tools/`.
3. If it's a new tool, follow the existing ones. Optional parameters need defaults, or the SDK treats them as required.

**`tools/HiveWE.Mcp/`:** the C# MCP server (.NET 10, `ModelContextProtocol` SDK). It runs over HTTP when HiveWE starts it, or over stdio. Its README covers setup.

**Testing:**
- Call the API directly: `curl -s http://127.0.0.1:47777/rpc -H "Content-Type: application/json" -d '{"jsonrpc":"2.0","id":1,"method":"map.info"}'`.
- `editor.quit` closes the editor without the "are you sure" prompt, which is needed before rebuilding because the exe is locked while it runs.

**MSVC 14.44 module pitfalls (each produced C1001 internal compiler errors here):**
- No module-scope `using json = nlohmann::json;` in a module: spell out `nlohmann::json`.
- In non-module `.cpp` files, never `#include` a header after an `import`, and never textually include `nlohmann/json.hpp` in a file that also imports HiveWE modules. Put such code in a module and `import <nlohmann/json.hpp>;`.
- To bisect a crash, rebuild with `MSBuild build/Release/HiveWE_lib.vcxproj -p:Configuration=Release`, since only changed modules recompile. `-t:ClCompile -p:SelectedFiles=...` silently skips `.ixx` files.

## Conventions

- Format with `.clang-format`: tabs (width 4), 140-column limit, braces always inserted, `int* p` pointer alignment, and a blank line between definitions.
- Commit messages are short imperative sentences without a prefix, e.g. "Fix out of bounds read" or "Add frame pacer".
