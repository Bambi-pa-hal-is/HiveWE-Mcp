using System.Net.Http.Json;
using System.Net.Sockets;
using System.Text.Json;
using System.Text.Json.Nodes;
using ModelContextProtocol;

namespace HiveWE.Mcp;

/// <summary>
/// Calls the JSON-RPC API that the running HiveWE editor serves on http://127.0.0.1:47777/rpc (src/api in the editor).
/// </summary>
public sealed class HiveWeClient(HttpClient http)
{
    public const string DefaultUrl = "http://127.0.0.1:47777/rpc";

    int nextId;

    /// <summary>
    /// Calls an editor method and returns its result. Editor errors and connection problems become
    /// <see cref="McpException"/>s so their message reaches the model.
    /// </summary>
    public async Task<JsonNode?> CallAsync(string method, JsonNode? parameters = null, CancellationToken cancellationToken = default)
    {
        var request = new JsonObject
        {
            ["jsonrpc"] = "2.0",
            ["id"] = Interlocked.Increment(ref nextId),
            ["method"] = method,
            ["params"] = parameters ?? new JsonObject(),
        };

        HttpResponseMessage response;
        try
        {
            response = await http.PostAsJsonAsync("", request, cancellationToken);
        }
        catch (HttpRequestException e) when (e.InnerException is SocketException)
        {
            throw new McpException(
                $"Could not reach HiveWE at {http.BaseAddress}. Is the editor running (without --no-api)?");
        }

        response.EnsureSuccessStatusCode();
        var body = await response.Content.ReadFromJsonAsync<JsonObject>(cancellationToken)
            ?? throw new McpException($"HiveWE sent an empty response to {method}");

        if (body["error"] is JsonObject error)
        {
            throw new McpException($"HiveWE error {error["code"]}: {error["message"]}");
        }
        return body["result"];
    }

    /// <summary>Calls a method with the tool's JSON arguments and returns the result as JSON text for the model.</summary>
    public async Task<string> CallForTextAsync(string method, JsonElement? args, CancellationToken cancellationToken)
    {
        var parameters = args is { ValueKind: JsonValueKind.Object } element ? JsonNode.Parse(element.GetRawText()) : null;
        var result = await CallAsync(method, parameters, cancellationToken);
        return result?.ToJsonString() ?? "null";
    }
}
