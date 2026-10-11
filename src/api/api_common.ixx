module;

#include <QBuffer>
#include <QByteArray>
#include <QImage>

export module ApiCommon;

import std;
import ApiError;
import Map;
import MapGlobal;
import Terrain;
import TriggerStrings;
import <glm/glm.hpp>;
import <nlohmann/json.hpp>;

// Spell out nlohmann::json in these modules: a module-scope `using json = nlohmann::json;` crashes MSVC 14.44 (C1001)

export namespace api {
	using Method = nlohmann::json (*)(const nlohmann::json& params);

	struct MethodEntry {
		const char* name;
		Method method;
	};

	Map& loaded_map() {
		if (map == nullptr || !map->loaded) {
			throw Error(error_code::no_map, "No map is loaded");
		}
		return *map;
	}

	/// Map texts may hold a TRIGSTR_ key instead of the text itself
	std::string resolve_string(const Map& m, const std::string& value) {
		if (value.starts_with("TRIGSTR_")) {
			return std::string(m.trigger_strings.string(value));
		}
		return value;
	}

	/// The API speaks world coordinates, the game units JASS uses. HiveWE stores positions in tiles from the bottom left corner.
	glm::vec2 world_to_internal(const Map& m, const glm::vec2 world) {
		return (world - m.terrain.offset) / 128.f;
	}

	glm::vec2 internal_to_world(const Map& m, const glm::vec2 internal) {
		return internal * 128.f + m.terrain.offset;
	}

	/// Encodes an image as PNG for the API, scaled down so its longest side is at most max_size
	nlohmann::json image_result(QImage image, const int max_size) {
		if (image.isNull()) {
			throw Error(error_code::internal_error, "Nothing to capture, is the editor window minimized?");
		}
		if (max_size > 0 && std::max(image.width(), image.height()) > max_size) {
			image = image.scaled(max_size, max_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		}

		QByteArray png;
		QBuffer buffer(&png);
		buffer.open(QIODevice::WriteOnly);
		image.save(&buffer, "PNG");

		return {
			{"mime_type", "image/png"},
			{"width", image.width()},
			{"height", image.height()},
			{"data", png.toBase64().toStdString()},
		};
	}

	/// Reads a required parameter, with a clear error when it is missing
	template <typename T>
	T required(const nlohmann::json& params, const char* name) {
		if (!params.contains(name)) {
			throw Error(error_code::invalid_params, std::format("Missing parameter \"{}\"", name));
		}
		return params.at(name).get<T>();
	}
} // namespace api
