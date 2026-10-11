#pragma once

// No includes on purpose: main.cpp includes this before its `import std;`

/// The editor's automation API: JSON-RPC 2.0 over HTTP on 127.0.0.1, used by the HiveWE MCP server (tools/HiveWE.Mcp).
/// The server lives in api_server.ixx, the methods in the api_*.ixx modules.
class HiveWE;

namespace api {
	constexpr unsigned short default_port = 47777;

	/// Starts the API server and stops it again when the application quits. Call after QApplication exists.
	void start(unsigned short port, HiveWE& window);
} // namespace api
