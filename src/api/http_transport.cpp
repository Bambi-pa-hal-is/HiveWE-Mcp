// httplib pulls in <winsock2.h>, keep windows.h from defining min/max and friends
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <httplib.h>

#include "http_transport.h"

#include <memory>
#include <print>
#include <thread>

namespace api::http {
	namespace {
		std::unique_ptr<httplib::Server> server;
		std::thread thread;
	} // namespace

	bool start(const unsigned short port, RequestHandler handler) {
		server = std::make_unique<httplib::Server>();
		server->Post("/rpc", [handler = std::move(handler)](const httplib::Request& request, httplib::Response& response) {
			// Browsers can't send application/json cross-origin without a preflight we never answer,
			// so this keeps web pages from driving the editor
			if (!request.get_header_value("Content-Type").starts_with("application/json")) {
				response.status = 415;
				response.set_content("Content-Type must be application/json", "text/plain");
				return;
			}

			const std::optional<std::string> result = handler(request.body);
			if (!result) {
				response.status = 204;
				return;
			}
			response.set_content(*result, "application/json");
		});

		if (!server->bind_to_port("127.0.0.1", port)) {
			std::println("API server: could not bind to 127.0.0.1:{}, is another HiveWE running?", port);
			server.reset();
			return false;
		}

		thread = std::thread([] {
			server->listen_after_bind();
		});
		std::println("API server listening on http://127.0.0.1:{}/rpc", port);
		return true;
	}

	void stop() {
		if (server) {
			server->stop();
		}
		if (thread.joinable()) {
			thread.join();
		}
		server.reset();
	}
} // namespace api::http
