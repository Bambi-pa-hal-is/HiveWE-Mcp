using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;

namespace HiveWE.Mcp.Tools;

[McpServerToolType]
public static class WorldTools
{
    [McpServerTool(Name = "hivewe_terrain")]
    [Description("""
        Edits the terrain with the editor's own terrain brush, so cliffs, ramps, pathing and object heights stay correct.
        Brush actions take x, y (world position of the brush center), size (tiles, default 4) and shape (circle, square, diamond):
        - raise {amount}: raises the ground by amount game units at the center, falling off to the edge. Negative lowers. water=true moves water too.
        - smooth {passes?}: smooths heights (default 30 passes; more is smoother).
        - flatten {height?}: flattens to the center's height, or to height (game units).
        - paint {tile}: paints a ground tile; tile must be one of the map's tiles (see tiles).
        - cliff {operation, cliff?}: operation raise, lower, raise2, lower2, level, deep_water, shallow_water or ramp. One cliff level is 128 units.
          Ramps go on the edge between two cliff levels. cliff picks the cliff type (one of the map's cliffs).
        - water {operation: add|remove}, boundary {operation: add|remove}: per-cell water and map boundary.
        Reading:
        - get {x, y, columns?, rows?}: corner data from (x, y) upwards and rightwards, max 64x64: height, cliff level, tile, water, ramp...
        - height_at {x, y}: interpolated ground height.
        - tiles {all?}: the tileset, the map's tiles and cliffs; all=true lists every tileset and tile id.
        - set_tiles {tiles, tileset?, replace?}: sets the map's tile list (max 16) and base tileset letter; replace maps removed tiles to new ones.
        - paint_grid {x, y, columns, rows, tiles}: sets the tile of every corner in a grid at once (one undo step). tiles is row-major from
          the corner at x/y upwards; "" keeps a corner. Corners are 128 apart. Best for rule-based texturing of large areas.
        - path {action, points: [[x, y], ...], spacing?, ...}: runs raise/smooth/flatten/paint/cliff/water/boundary/pathing with its usual
          arguments along a polyline (spacing defaults to a third of the brush). One undo step. Use it for roads and rivers.
        water also takes height (world units of the surface): water within 51 units above the ground is shallow and walkable.
        """)]
    public static Task<string> Terrain(
        HiveWeClient hivewe,
        [Description("raise, smooth, flatten, paint, cliff, water, boundary, get, height_at, tiles or set_tiles")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "terrain", action, ["raise", "smooth", "flatten", "paint", "cliff", "water", "boundary", "get", "height_at", "tiles", "set_tiles", "paint_grid", "path"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_pathing")]
    [Description("""
        The pathing map, 4 cells per tile. Terrain tiles, cliffs and water set pathing automatically; this overrides it.
        - set {x, y, size? (tiles, default 1), shape? (square|circle), walkable?, flyable?, buildable?, blight?}: true allows, false blocks.
        - get {x, y}: walkable, flyable, buildable, blight, water at a point, including doodad footprints.
        """)]
    public static Task<string> Pathing(
        HiveWeClient hivewe,
        [Description("set or get")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "pathing", action, ["set", "get"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_doodads")]
    [Description("""
        Placed doodads and destructibles (trees, rocks, buildings decoration...).
        - list {type?, x?, y?, radius? | left?, bottom?, right?, top?, limit?}: placed doodads, filtered by type and area.
        - get {id}
        - create {type, x, y, facing?, scale? (number or [x,y,z]), variation?, z?, state?, life?, skin?, item_drops?}
          or {doodads: [{...}, ...]} to place many in one undo step. Doodads with a footprint snap to the pathing grid.
        - update {id or ids, ...same fields as create}: changes the given fields.
        - delete {id or ids} or {type?, area filter}: removes doodads.
        - scatter {type or types, x, y, radius | left, bottom, right, top, count | density (per 1000x1000), min_distance? (100),
          scale? [min, max], avoid? [{x, y, radius} | {points, width}], avoid_water? (true), avoid_unwalkable? (true), seed?}:
          random placement with random facing, for forests, flowers and rubble. One undo step.
        - models {type}: which variations of a type have a model. Variations without one show as green boxes; created doodads
          only pick random variations that have models.
        state: invisible_non_solid, visible_non_solid, visible_solid. item_drops: [[{"type": "ratc", "chance": 100}], ...] (one list per drop set).
        """)]
    public static Task<string> Doodads(
        HiveWeClient hivewe,
        [Description("list, get, create, update or delete")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "doodads", action, ["list", "get", "create", "update", "delete", "scatter", "models"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_units")]
    [Description("""
        Units placed on the map (start locations are set with hivewe_players).
        - list {type?, player?, x?, y?, radius? | left?, bottom?, right?, top?, limit?}
        - get {id}
        - create {type, x, y, player?, facing?, ...} or {units: [{...}, ...]}
        - update {id or ids, ...}
        - delete {id or ids} or {type?, player?, area filter}
        Fields: player (0-23; 24 neutral hostile, 27 neutral passive), facing, scale, variation, hp and mana (-1 = default,
        hp as percent), level, strength, agility, intelligence (heroes), gold (gold mines), target_acquisition (-1 default,
        -2 camp), custom_color (-1 = player color), waygate (region id or -1), skin, inventory [{"slot": 0, "type": "ratc"}],
        abilities [{"type": "AHbz", "level": 1, "autocast": false}], item_drops (like doodads).
        """)]
    public static Task<string> Units(
        HiveWeClient hivewe,
        [Description("list, get, create, update or delete")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "units", action, ["list", "get", "create", "update", "delete"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_items")]
    [Description("""
        Items lying on the ground. list {type?, area filter?}, create {type, x, y, ...} or {items: [...]},
        update {id or ids, x?, y?, ...}, delete {id or ids} or {type?, area filter}.
        """)]
    public static Task<string> Items(
        HiveWeClient hivewe,
        [Description("list, create, update or delete")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "items", action, ["list", "create", "update", "delete"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_regions")]
    [Description("""
        Regions (rects), available in scripts as gg_rct_<name with underscores>.
        list, create {name?, left, bottom, right, top, color? [r,g,b], weather? (e.g. RAhr), ambient_sound?},
        update {id or name, new_name?, ...}, delete {id or name}. Names must be unique.
        """)]
    public static Task<string> Regions(
        HiveWeClient hivewe,
        [Description("list, create, update or delete")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "regions", action, ["list", "create", "update", "delete"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_game_cameras")]
    [Description("""
        Game camera setups (gg_cam_<name>) for cinematics and CameraSetupApply. list, create {name?, x, y, distance?,
        rotation? (degrees, 90 = facing north), angle_of_attack? (degrees, 304 = WC3 default), fov?, roll?, z_offset?, far_z?, near_z?},
        update {index or name, new_name?, ...}, delete {index or name}.
        """)]
    public static Task<string> GameCameras(
        HiveWeClient hivewe,
        [Description("list, create, update or delete")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "cameras", action, ["list", "create", "update", "delete"], args, cancellationToken);
}
