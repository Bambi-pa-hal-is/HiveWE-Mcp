using System.Text.Json;
using ModelContextProtocol;

namespace HiveWE.Mcp.Tools;

/// <summary>Maps an entity tool's action onto the editor method "&lt;prefix&gt;.&lt;action&gt;".</summary>
static class Dispatch
{
    public static Task<string> Run(HiveWeClient hivewe, string prefix, string action, string[] actions, JsonElement? args, CancellationToken cancellationToken)
    {
        if (!actions.Contains(action))
        {
            throw new McpException($"Unknown action \"{action}\", expected one of: {string.Join(", ", actions)}");
        }
        return hivewe.CallForTextAsync($"{prefix}.{action}", args, cancellationToken);
    }
}
