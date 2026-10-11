export module ApiError;

import std;

export namespace api {
	/// Bumped whenever a method changes in a way clients must know about
	constexpr int version = 1;

	/// JSON-RPC 2.0 error codes. The -320xx range is ours
	namespace error_code {
		constexpr int parse_error = -32700;
		constexpr int invalid_request = -32600;
		constexpr int method_not_found = -32601;
		constexpr int invalid_params = -32602;
		constexpr int internal_error = -32603;
		constexpr int timeout = -32001;
		constexpr int no_map = -32002;
		constexpr int shutting_down = -32003;
	} // namespace error_code

	/// Thrown by method handlers to answer with a JSON-RPC error instead of a result
	struct Error : std::runtime_error {
		int code;

		Error(const int code, const std::string& message) : std::runtime_error(message), code(code) {}
	};
} // namespace api
