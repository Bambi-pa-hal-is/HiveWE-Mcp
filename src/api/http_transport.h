#pragma once

#include <functional>
#include <optional>
#include <string>

/// The HTTP side of the API, kept out of the modules: exporting httplib through a module interface crashes MSVC
namespace api::http {
	/// Called on a server thread with a request body. Returns the JSON response, or nothing for 204 No Content
	using RequestHandler = std::function<std::optional<std::string>(const std::string& body)>;

	/// Serves POST /rpc on 127.0.0.1:port on a background thread. Returns false if the port is taken.
	bool start(unsigned short port, RequestHandler handler);

	/// Stops serving and waits for in-flight requests to finish
	void stop();
} // namespace api::http
