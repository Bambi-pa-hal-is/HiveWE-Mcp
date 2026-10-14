using System.ComponentModel;
using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Nodes;
using ModelContextProtocol;
using ModelContextProtocol.Server;

namespace HiveWE.Mcp.Tools;

[McpServerToolType]
public static class ScriptTools
{
    [McpServerTool(Name = "hivewe_script")]
    [Description("""
        The map script, JASS or Lua depending on hivewe_map_info script_language.
        - get_header: the map header code (custom script section: globals, libraries, helpers).
        - set_header {code, comment?}: replaces it.
        - build: generates and compiles the whole map script without saving (JASS through JassHelper/vJass, Lua through a Lua parser) and returns errors.
        - generated {from_line?, line_count?}: reads the generated war3map.j/war3map.lua with line numbers, to look up compile errors.
        """)]
    public static Task<string> Script(
        HiveWeClient hivewe,
        [Description("get_header, set_header, build or generated")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default)
    {
        var methods = new Dictionary<string, string>
        {
            ["get_header"] = "script.header.get",
            ["set_header"] = "script.header.set",
            ["build"] = "script.build",
            ["generated"] = "script.generated",
        };
        if (!methods.TryGetValue(action, out var method))
        {
            throw new McpException($"Unknown action \"{action}\", expected one of: {string.Join(", ", methods.Keys)}");
        }
        return hivewe.CallForTextAsync(method, args, cancellationToken);
    }

    [McpServerTool(Name = "hivewe_triggers")]
    [Description("""
        Script triggers (custom text triggers). list; get {id or name}; create {name, code, category?, enabled?, run_on_initialization?, description?};
        update {id or name, new_name?, code?, ...}; delete {id or name}.
        The code of a trigger named "My Trigger" must define InitTrig_My_Trigger (JASS: function InitTrig_My_Trigger takes nothing returns nothing,
        Lua: function InitTrig_My_Trigger()), which usually creates gg_trg_My_Trigger = CreateTrigger() and registers events.
        Results include warnings when that function is missing. GUI triggers can be listed and deleted only.
        """)]
    public static Task<string> Triggers(
        HiveWeClient hivewe,
        [Description("list, get, create, update or delete")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "triggers", action, ["list", "get", "create", "update", "delete"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_variables")]
    [Description("""
        Global variables, available in scripts as udg_<name>. list; create {name, type, is_array?, array_size?, initial_value?};
        update {name, new_name?, type?, ...}; delete {name}. Types are trigger types: integer, real, boolean, string, unit,
        unitcode, player, location, rect, timer, group, force, sound, effect, ...
        """)]
    public static Task<string> Variables(
        HiveWeClient hivewe,
        [Description("list, create, update or delete")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "variables", action, ["list", "create", "update", "delete"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_strings")]
    [Description("""
        Trigger strings (war3map.wts), the localizable texts referenced as TRIGSTR_xxx. list {search?}; set {key?, value}:
        changes a key's text, or creates a new key without key and returns it.
        """)]
    public static Task<string> Strings(
        HiveWeClient hivewe,
        [Description("list or set")] string action,
        [Description("The action's arguments as a JSON object")] JsonElement? args = null,
        CancellationToken cancellationToken = default) =>
        Dispatch.Run(hivewe, "strings", action, ["list", "set"], args, cancellationToken);

    [McpServerTool(Name = "hivewe_build_typescript")]
    [Description("""
        Builds a TypeScript-to-Lua (TSTL / w3ts) project and loads the resulting Lua bundle into the map header.
        Runs command (default "npm run build") in project_dir, then reads output_file (the TSTL luaBundle, relative to project_dir)
        and sets it as the header code. Switches the map to Lua. The bundle must be only the compiled TypeScript (w3ts hooks into
        main itself), not a complete war3map.lua. Returns the build output and whether it loaded.
        """)]
    public static async Task<string> BuildTypeScript(
        HiveWeClient hivewe,
        [Description("The TypeScript project folder")] string project_dir,
        [Description("The bundle the build produces, relative to project_dir, e.g. dist/tstl_output.lua")] string output_file,
        [Description("The build command, default \"npm run build\"")] string? command = null,
        CancellationToken cancellationToken = default)
    {
        if (!Directory.Exists(project_dir))
        {
            throw new McpException($"No such folder: {project_dir}");
        }

        var start = new ProcessStartInfo("cmd.exe", ["/c", command ?? "npm run build"])
        {
            WorkingDirectory = project_dir,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
        };
        using var process = Process.Start(start) ?? throw new McpException("Starting the build failed");
        var stdout = process.StandardOutput.ReadToEndAsync(cancellationToken);
        var stderr = process.StandardError.ReadToEndAsync(cancellationToken);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromMinutes(5));
        try
        {
            await process.WaitForExitAsync(timeout.Token);
        }
        catch (OperationCanceledException)
        {
            process.Kill(entireProcessTree: true);
            throw new McpException("The build took longer than 5 minutes and was stopped");
        }

        var output = Tail((await stdout) + (await stderr), 4000);
        if (process.ExitCode != 0)
        {
            return JsonSerializer.Serialize(new { loaded = false, exit_code = process.ExitCode, output });
        }

        var bundle = Path.Combine(project_dir, output_file);
        if (!File.Exists(bundle))
        {
            throw new McpException($"The build succeeded but {bundle} doesn't exist. Output:\n{output}");
        }
        var code = await File.ReadAllTextAsync(bundle, cancellationToken);
        await hivewe.CallAsync("mapinfo.set", new JsonObject { ["script_language"] = "lua" }, cancellationToken);
        await hivewe.CallAsync("script.header.set", new JsonObject { ["code"] = code }, cancellationToken);
        return JsonSerializer.Serialize(new { loaded = true, characters = code.Length, output });
    }

    static string Tail(string text, int length) => text.Length <= length ? text : "..." + text[^length..];
}
