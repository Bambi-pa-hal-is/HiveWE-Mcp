#pragma once

// No includes on purpose: api.cpp includes this before its `import std;`

class HiveWE;

namespace api {
	constexpr unsigned short default_mcp_port = 47778;

	/// Adds the MCP tab to the ribbon and, when `start_server` is set, starts the MCP server (tools/HiveWE.Mcp) in HTTP
	/// mode so that MCP clients can connect by URL. The server is stopped when HiveWE quits.
	void install_mcp_panel(HiveWE& window, unsigned short api_port, bool api_running, bool start_server);
} // namespace api
