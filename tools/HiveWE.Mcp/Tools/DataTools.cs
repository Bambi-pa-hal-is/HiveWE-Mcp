using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;

namespace HiveWE.Mcp.Tools;

[McpServerToolType]
public static class DataTools
{
    [McpServerTool(Name = "hivewe_objects")]
    [Description("""
        Object editor data: the definitions of unit, item, ability, doodad, destructible, upgrade and buff types.
        Every action takes kind (unit, item, ability, doodad, destructible, upgrade, buff).
        - list {kind, search?, custom_only?, limit?}: object ids and names. search matches name or id.
        - search {search, limit?}: searches every kind at once (no kind needed), e.g. trees are destructibles, not doodads.
        - get {kind, id, fields? [names] | all?}: the modified fields, plus the requested ones or all field values.
        - fields {kind, id, search?}: the editable fields with display name, type, category, level and limits.
        - create {kind, base, id?, name?, fields? {field: value}}: copies a standard object into a new custom one (id generated when omitted).
        - set {kind, id, fields {field: value}}: changes fields. Placed instances update live.
        - reset {kind, id, fields?}: restores fields to their defaults (all modified fields without fields).
        - delete {kind, id}: deletes a custom object and its placed instances.
        Field names are lowercase SLK names, e.g. hp, def, spd, abillist (comma list or JSON list), name, file (model path).
        Leveled fields end in the level: cool1, dataa2, hbz31... Use fields to discover them. Values may be strings, numbers, booleans or lists.
        """)]
    public static Task<string> Objects(
        HiveWeClient hivewe,
        [Description("list, get, fields, create, set, reset or delete")] string action,
        [Description("The action's arguments as a JSON object, always with kind")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "objects", action, ["list", "search", "get", "fields", "create", "set", "reset", "delete"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_gameplay_constants")]
    [Description("""
        Gameplay constants (war3mapMisc.txt), e.g. maxherolevel, gamedaylength, heroexpgainrange.
        get {fields? | all?} (without arguments: the modified ones), set {fields {name: value}}, reset {fields?}.
        """)]
    public static Task<string> GameplayConstants(
        HiveWeClient hivewe,
        [Description("get, set or reset")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "constants", action, ["get", "set", "reset"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_map_info")]
    [Description("""
        Scenario settings (war3map.w3i). get, or set with any of:
        name, author, description, suggested_players, loading_screen_text/title/subtitle, prologue_text/title/subtitle,
        loading_screen_model, loading_screen_number, prologue_screen_model, script_language (jass|lua), game_data_set,
        options {hide_minimap_preview, melee_map, fixed_player_settings, custom_forces, use_terrain_fog, water_tinting,
        item_classification, masked_area_partially_visible, force_default_zoom, ...}, fog {style, start_z, end_z, density, color [r,g,b]},
        weather (4 letter id, e.g. RAhr rain, SNls snow, empty for none), sound_environment, light_tileset, ambience_tileset (tileset letters),
        water_color [r,g,b,a], camera_distance {default, min, max}.
        """)]
    public static Task<string> MapInfo(
        HiveWeClient hivewe,
        [Description("get or set")] string action,
        [Description("For set: the settings to change")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "mapinfo", action, ["get", "set"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_players")]
    [Description("""
        Player slots. list; set {number (0-23), name?, controller? (human|computer|neutral|rescuable), race? (selectable|human|orc|undead|night_elf),
        fixed_start?, start_x?, start_y?, ally_low_priority? [numbers], ally_high_priority? [numbers]} creates or updates a player;
        start_x/start_y place its start location. delete {number}.
        """)]
    public static Task<string> Players(
        HiveWeClient hivewe,
        [Description("list, set or delete")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "players", action, ["list", "set", "delete"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_forces")]
    [Description("""
        Forces (teams). list, or set {forces: [{name, players: [numbers], allied?, allied_victory?, share_vision?,
        share_unit_control?, share_advanced_unit_control?}], custom_forces? (default true)} which replaces all forces.
        """)]
    public static Task<string> Forces(
        HiveWeClient hivewe,
        [Description("list or set")] string action,
        [Description("For set: the forces")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "forces", action, ["list", "set"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_tech")]
    [Description("""
        Tech availability per player, applied at map start. get, or set {unavailable?: [{id, players?}], upgrades?: [{id, level, availability
        (unavailable|available|researched), players?}]}. unavailable lists units, buildings, items or abilities players can't use.
        Each given list replaces the old one; players defaults to all.
        """)]
    public static Task<string> Tech(
        HiveWeClient hivewe,
        [Description("get or set")] string action,
        [Description("For set: the lists")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "tech", action, ["get", "set"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_random_tables")]
    [Description("""
        Random item tables and random unit tables. get, or set {item_tables?: [{number?, name, sets: [[{type, chance}]]}],
        unit_tables?: [{number?, name, positions: [0], lines: [{chance, ids: [unit ids]}]}]} which replaces the given lists.
        Item tables become ItemTable_<number> drop functions in the map script.
        """)]
    public static Task<string> RandomTables(
        HiveWeClient hivewe,
        [Description("get or set")] string action,
        [Description("For set: the tables")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "random_tables", action, ["get", "set"], args, cancellationToken);
}
