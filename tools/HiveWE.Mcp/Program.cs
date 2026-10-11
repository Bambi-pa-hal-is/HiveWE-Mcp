using HiveWE.Mcp;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

// Two ways to run:
// - stdio (default): an MCP client such as Claude Code starts this process itself.
// - --http [--port 47778]: serves MCP on http://127.0.0.1:<port>/mcp. HiveWE starts it like this, so launching the editor
//   starts everything and clients connect by URL.
// --api-url (or the HIVEWE_API_URL environment variable) points at the editor's JSON-RPC API.
var http = args.Contains("--http");
var port = OptionValue("--port") is { } portText ? int.Parse(portText) : 47778;
var apiUrl = OptionValue("--api-url") ?? Environment.GetEnvironmentVariable("HIVEWE_API_URL") ?? HiveWeClient.DefaultUrl;

void AddServices(IServiceCollection services, Action<IMcpServerBuilder> transport)
{
    services.AddHttpClient<HiveWeClient>(client =>
    {
        client.BaseAddress = new Uri(apiUrl);
        // Editor calls wait up to 60s for the editor's main thread
        client.Timeout = TimeSpan.FromSeconds(90);
    });
    var builder = services.AddMcpServer(options => options.ServerInstructions = Instructions.Text).WithToolsFromAssembly();
    transport(builder);
}

if (http)
{
    var builder = WebApplication.CreateBuilder(args);
    builder.WebHost.UseUrls($"http://127.0.0.1:{port}");
    AddServices(builder.Services, mcp => mcp.WithHttpTransport());
    var app = builder.Build();
    app.MapMcp("/mcp");
    await app.RunAsync();
}
else
{
    var builder = Host.CreateApplicationBuilder(args);
    // stdout carries the MCP protocol, so all logging goes to stderr
    builder.Logging.AddConsole(options => options.LogToStandardErrorThreshold = LogLevel.Trace);
    AddServices(builder.Services, mcp => mcp.WithStdioServerTransport());
    await builder.Build().RunAsync();
}

string? OptionValue(string name)
{
    var index = Array.IndexOf(args, name);
    return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
}
