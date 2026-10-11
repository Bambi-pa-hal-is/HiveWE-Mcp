module;

#include <QCoreApplication>
#include <QMetaObject>

export module ApiServer;

import std;
import ApiError;
import <nlohmann/json.hpp>;

export namespace api {
	using Handler = std::function<nlohmann::json(const nlohmann::json& params)>;

	/// Dispatches JSON-RPC 2.0 requests to registered methods. The HTTP side lives in http_transport.cpp.
	/// Every handler runs on the Qt main thread, so handlers can touch the map and the UI freely.
	class Server {
		/// How long a request waits for the main thread before giving up, e.g. when a modal dialog blocks it
		static constexpr auto main_thread_timeout = std::chrono::seconds(60);

		std::unordered_map<std::string, Handler> methods;
		/// Set while shutting down so waiting requests stop waiting on a main thread that no longer runs events
		std::atomic<bool> stopping = false;

		static nlohmann::json error_response(const nlohmann::json& id, const int code, const std::string& message) {
			return {
				{"jsonrpc", "2.0"},
				{"id", id},
				{"error", {{"code", code}, {"message", message}}},
			};
		}

		/// Runs the handler on the Qt main thread and waits for its result
		nlohmann::json call_on_main_thread(const Handler& handler, const nlohmann::json& params) {
			auto promise = std::make_shared<std::promise<nlohmann::json>>();
			std::future<nlohmann::json> result = promise->get_future();

			QMetaObject::invokeMethod(
				qApp,
				[promise, handler, params] {
					try {
						promise->set_value(handler(params));
					} catch (...) {
						promise->set_exception(std::current_exception());
					}
				},
				Qt::QueuedConnection
			);

			const auto deadline = std::chrono::steady_clock::now() + main_thread_timeout;
			while (result.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
				if (stopping) {
					throw Error(error_code::shutting_down, "The editor is shutting down");
				}
				if (std::chrono::steady_clock::now() > deadline) {
					throw Error(error_code::timeout, "Timed out waiting for the editor, is a dialog open?");
				}
			}
			return result.get();
		}

		std::optional<nlohmann::json> handle_json(const std::string& body) {
			nlohmann::json request;
			try {
				request = nlohmann::json::parse(body);
			} catch (const nlohmann::json::parse_error& e) {
				return error_response(nullptr, error_code::parse_error, e.what());
			}

			if (!request.is_object() || !request.contains("method") || !request["method"].is_string()) {
				return error_response(nullptr, error_code::invalid_request, "Expected an object with a string \"method\"");
			}

			const bool is_notification = !request.contains("id");
			const nlohmann::json id = request.value("id", nlohmann::json());
			const std::string method = request["method"];
			const nlohmann::json params = request.value("params", nlohmann::json::object());

			Handler handler;
			if (method == "batch") {
				handler = [this](const nlohmann::json& batch_params) {
					return run_batch(batch_params);
				};
			} else if (const auto found = methods.find(method); found != methods.end()) {
				handler = found->second;
			} else {
				return error_response(id, error_code::method_not_found, "Unknown method: " + method);
			}

			nlohmann::json response;
			try {
				response = {
					{"jsonrpc", "2.0"},
					{"id", id},
					{"result", call_on_main_thread(handler, params)},
				};
			} catch (const Error& e) {
				response = error_response(id, e.code, e.what());
			} catch (const nlohmann::json::exception& e) {
				// Mostly params.at() on a missing or mistyped parameter
				response = error_response(id, error_code::invalid_params, e.what());
			} catch (const std::exception& e) {
				response = error_response(id, error_code::internal_error, e.what());
			} catch (...) {
				response = error_response(id, error_code::internal_error, "Unknown error");
			}

			if (is_notification) {
				return std::nullopt;
			}
			return response;
		}

		/// Runs {"calls": [{"method", "params"}, ...]} in order on the main thread. Stops at the first error unless
		/// "stop_on_error" is false. The map edits of the whole batch undo as one step.
		nlohmann::json run_batch(const nlohmann::json& params) {
			if (!params.contains("calls") || !params["calls"].is_array()) {
				throw Error(error_code::invalid_params, "batch takes \"calls\": [{\"method\": ..., \"params\": {...}}, ...]");
			}
			const bool stop_on_error = params.value("stop_on_error", true);
			const std::size_t mark = undo_mark ? undo_mark() : 0;

			nlohmann::json results = nlohmann::json::array();
			bool failed = false;
			for (const auto& call : params["calls"]) {
				const std::string name = call.value("method", "");
				const auto found = methods.find(name);
				try {
					if (found == methods.end()) {
						throw Error(error_code::method_not_found, "Unknown method: " + name);
					}
					results.push_back({{"result", found->second(call.value("params", nlohmann::json::object()))}});
				} catch (const Error& e) {
					results.push_back({{"error", {{"code", e.code}, {"message", e.what()}}}});
					failed = true;
				} catch (const std::exception& e) {
					results.push_back({{"error", {{"code", error_code::invalid_params}, {"message", e.what()}}}});
					failed = true;
				}
				if (failed && stop_on_error) {
					break;
				}
			}

			if (undo_merge) {
				undo_merge(mark);
			}
			return {{"completed", results.size()}, {"failed", failed}, {"results", results}};
		}

	  public:
		/// Lets batch merge its undo steps: the first returns a mark, the second merges everything after it
		std::function<std::size_t()> undo_mark;
		std::function<void(std::size_t)> undo_merge;

		/// Must be called before requests arrive
		void register_method(const std::string& name, Handler handler) {
			methods[name] = std::move(handler);
		}

		/// Handles one JSON-RPC request from any thread. Returns nothing for notifications (requests without an id)
		std::optional<std::string> handle(const std::string& body) {
			const std::optional<nlohmann::json> response = handle_json(body);
			if (!response) {
				return std::nullopt;
			}
			return response->dump();
		}

		/// Makes requests that are waiting on the main thread give up, so shutdown doesn't hang
		void stop() {
			stopping = true;
		}
	};
} // namespace api
