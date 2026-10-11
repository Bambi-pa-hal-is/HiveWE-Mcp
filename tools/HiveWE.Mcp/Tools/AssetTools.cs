using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;

namespace HiveWE.Mcp.Tools;

[McpServerToolType]
public static class AssetTools
{
    [McpServerTool(Name = "hivewe_imports")]
    [Description("""
        Files imported into the map (custom models, textures, icons, sounds). list {extension?}; add {source (file on disk),
        path? (inside the map, default war3mapImported/<file name>)}; delete {path}.
        Import a model and its textures before pointing an object's model field (file) at it, e.g. war3mapImported/Dragon.mdx.
        """)]
    public static Task<string> Imports(
        HiveWeClient hivewe,
        [Description("list, add or delete")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "imports", action, ["list", "add", "delete"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_sounds")]
    [Description("""
        Sound definitions, available in scripts as gg_snd_<name>. list; create {name, file (game or imported path), looping?, is_3d?,
        stop_out_of_range?, music?, volume? (-1 default), pitch?, fade_in_rate?, fade_out_rate?, channel?, eax_effect?, min_distance?,
        max_distance?, distance_cutoff?}; update {name, new_name?, ...}; delete {name}.
        """)]
    public static Task<string> Sounds(
        HiveWeClient hivewe,
        [Description("list, create, update or delete")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "sounds", action, ["list", "create", "update", "delete"], args, cancellationToken);
}
