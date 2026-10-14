using System.ComponentModel;
using System.Text.Json;
using System.Text.Json.Nodes;
using ModelContextProtocol;
using ModelContextProtocol.Protocol;
using ModelContextProtocol.Server;

namespace HiveWE.Mcp.Tools;

[McpServerToolType]
public static class EditorTools
{
    [McpServerTool(Name = "hivewe_status", ReadOnly = true)]
    [Description("Checks that HiveWE is running and returns the editor version, API version, Warcraft III directory, graphics mode (SD/HD/DE) and locale.")]
    public static Task<string> Status(HiveWeClient hivewe, CancellationToken cancellationToken = default) =>
        hivewe.CallForTextAsync("editor.version", null, cancellationToken);

    [McpServerTool(Name = "hivewe_map")]
    [Description("""
        Map lifecycle. action:
        - info: name, size in tiles, offset_x/offset_y (world position of the bottom left corner), tileset, script language, object counts.
        - new {width?, height?}: creates a flat 64x64 (or given, multiples of 32 up to 480) Lordaeron Summer map in a temporary folder. Discards the open map without asking.
        - open {path, unpack_to?}: opens a folder map, or unpacks a .w3x/.w3m next to it (or into unpack_to) and opens that.
        - save {path?}: saves to its folder, or to path (the map folder itself, which must be empty or new). New maps need a path.
        - export {path, protect?}: saves and packs a .w3x. protect=true strips editor-only files so it can't be reopened.
        - test: saves and launches Warcraft III on the map.
        Returns the result as JSON, including script compile warnings for save/test/export.
        """)]
    public static Task<string> Map(
        HiveWeClient hivewe,
        [Description("info, new, open, save, export or test")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "map", action, ["info", "new", "open", "save", "export", "test"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_save")]
    [Description("""
        Saves the open map to its folder, or to path (the map folder itself, which must be empty or new; new maps need one).
        autosave_minutes turns on saving every N minutes (0 turns it off). Returns the folder and any script compile warnings.
        """)]
    public static async Task<string> Save(
        HiveWeClient hivewe,
        [Description("Folder to save to, for new maps or save as")] string? path = null,
        [Description("Also save automatically every this many minutes, 0 to stop")] int? autosave_minutes = null,
        CancellationToken cancellationToken = default)
    {
        var parameters = new JsonObject();
        if (path is not null)
        {
            parameters["path"] = path;
        }
        var result = await hivewe.CallAsync("map.save", parameters, cancellationToken);
        if (autosave_minutes is { } minutes)
        {
            await hivewe.CallAsync("map.autosave", new JsonObject { ["minutes"] = minutes }, cancellationToken);
        }
        return result?.ToJsonString() ?? "null";
    }

    [McpServerTool(Name = "hivewe_overview", ReadOnly = true)]
    [Description("The whole map from straight above, with terrain, doodads, units and water, as one image. Use it to review the layout.")]
    public static Task<IEnumerable<ContentBlock>> Overview(
        HiveWeClient hivewe,
        [Description("Longest side of the image in pixels, default 1024")] int? max_size = null,
        CancellationToken cancellationToken = default)
    {
        var parameters = new JsonObject();
        AddIfSet(parameters, "max_size", max_size);
        return Image(hivewe, "view.overview", parameters, cancellationToken);
    }

    [McpServerTool(Name = "hivewe_view")]
    [Description("""
        What the editor view draws. action get, or set with any of: units, doodads, pathing, regions, brush, lighting, water,
        click_helpers (the green boxes on small doodads, editor only), wireframe. Turn click_helpers off for clean screenshots,
        pathing or regions on to check them.
        """)]
    public static Task<string> View(
        HiveWeClient hivewe,
        [Description("get or set")] string action,
        [Description("For set: the toggles, e.g. {\"click_helpers\": false}")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "view", action, ["get", "set"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_game_log", ReadOnly = true)]
    [Description("The end of Warcraft III's newest log files (Documents/Warcraft III/Logs and Errors), to find script errors after hivewe_map test.")]
    public static Task<string> GameLog(
        HiveWeClient hivewe,
        [Description("Lines per file, default 80")] int? lines = null,
        CancellationToken cancellationToken = default)
    {
        var parameters = new JsonObject();
        AddIfSet(parameters, "lines", lines);
        return hivewe.CallForTextAsync("game.log", JsonSerializer.SerializeToElement(parameters), cancellationToken);
    }

    [McpServerTool(Name = "hivewe_screenshot", ReadOnly = true)]
    [Description("""
        Renders the map and returns it as an image (offscreen, so it works while the editor is minimized). Optionally moves the camera first:
        x, y (world position looked at), distance (game units, about 1500-4000 for an overview), yaw (degrees, 0 looks north),
        pitch (degrees, negative looks down, e.g. -60). max_size limits the longest side in pixels (default 1024).
        """)]
    public static async Task<IEnumerable<ContentBlock>> Screenshot(
        HiveWeClient hivewe,
        [Description("World x to look at")] float? x = null,
        [Description("World y to look at")] float? y = null,
        [Description("Camera distance in game units")] float? distance = null,
        [Description("Camera yaw in degrees")] float? yaw = null,
        [Description("Camera pitch in degrees, negative looks down")] float? pitch = null,
        [Description("Longest side of the image in pixels, default 1024")] int? max_size = null,
        [Description("Render width in pixels, default 1280")] int? width = null,
        [Description("Render height in pixels, default 720")] int? height = null,
        CancellationToken cancellationToken = default)
    {
        var parameters = new JsonObject();
        AddIfSet(parameters, "width", width);
        AddIfSet(parameters, "height", height);
        AddIfSet(parameters, "x", x);
        AddIfSet(parameters, "y", y);
        AddIfSet(parameters, "distance", distance);
        AddIfSet(parameters, "yaw", yaw);
        AddIfSet(parameters, "pitch", pitch);
        AddIfSet(parameters, "max_size", max_size);
        return await Image(hivewe, "view.screenshot", parameters, cancellationToken);
    }

    [McpServerTool(Name = "hivewe_minimap", ReadOnly = true)]
    [Description("Returns the terrain minimap image (tiles, cliffs and water; no doodads or units), one pixel per tile corner. Good for a quick layout overview.")]
    public static Task<IEnumerable<ContentBlock>> Minimap(
        HiveWeClient hivewe,
        [Description("Longest side of the image in pixels, default 512")] int? max_size = null,
        CancellationToken cancellationToken = default)
    {
        var parameters = new JsonObject();
        AddIfSet(parameters, "max_size", max_size);
        return Image(hivewe, "view.minimap", parameters, cancellationToken);
    }

    [McpServerTool(Name = "hivewe_camera")]
    [Description("The editor camera. action get, or set {x?, y?, distance?, yaw?, pitch?} (world units and degrees). hivewe_screenshot can also move it.")]
    public static Task<string> Camera(
        HiveWeClient hivewe,
        [Description("get or set")] string action,
        [Description("For set: the camera values to change")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "camera", action, ["get", "set"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_undo")]
    [Description("Undoes (action=undo) or redoes (action=redo) the last map edit: terrain, pathing, doodads, units, items or regions. Object data, map info and scripts are not undoable.")]
    public static Task<string> Undo(
        HiveWeClient hivewe,
        [Description("undo or redo")] string action,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "edit", action, ["undo", "redo"], null, cancellationToken);

    [McpServerTool(Name = "hivewe_batch")]
    [Description("""
        Runs many editor calls in one request, in order, e.g. placing hundreds of doodads. calls is a list of
        {"method": "<editor method>", "params": {...}}. Editor methods are the tools' "<tool>.<action>" names,
        e.g. doodads.create, units.update, terrain.paint, objects.set. Stops at the first error unless stop_on_error is false.
        All map edits of the batch undo as one step. Returns every call's result or error.
        """)]
    public static async Task<string> Batch(
        HiveWeClient hivewe,
        [Description("The calls: [{\"method\": \"doodads.create\", \"params\": {...}}, ...]")] JsonElement calls,
        [Description("Keep going after a failed call, default false")] bool? continue_on_error = null,
        CancellationToken cancellationToken = default)
    {
        var parameters = new JsonObject
        {
            ["calls"] = JsonNode.Parse(calls.GetRawText()),
            ["stop_on_error"] = continue_on_error != true,
        };
        var result = await hivewe.CallAsync("batch", parameters, cancellationToken);
        return result?.ToJsonString() ?? "null";
    }

    [McpServerTool(Name = "hivewe_call")]
    [Description("Calls any editor API method directly with JSON params, for methods no other tool covers. Prefer the specific tools.")]
    public static Task<string> Call(
        HiveWeClient hivewe,
        [Description("The editor method, e.g. terrain.height_at")] string method,
        [Description("The method's params as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        hivewe.CallForTextAsync(method, args, cancellationToken);

    static void AddIfSet<T>(JsonObject target, string name, T? value) where T : struct
    {
        if (value.HasValue)
        {
            target[name] = JsonValue.Create(value.Value);
        }
    }

    static async Task<IEnumerable<ContentBlock>> Image(HiveWeClient hivewe, string method, JsonObject parameters, CancellationToken cancellationToken = default)
    {
        var result = await hivewe.CallAsync(method, parameters, cancellationToken) as JsonObject
            ?? throw new McpException("HiveWE returned no image");
        var bytes = Convert.FromBase64String(result["data"]!.GetValue<string>());
        return
        [
            ImageContentBlock.FromBytes(bytes, result["mime_type"]!.GetValue<string>()),
            new TextContentBlock { Text = $"{result["width"]}x{result["height"]} pixels" },
        ];
    }
}
