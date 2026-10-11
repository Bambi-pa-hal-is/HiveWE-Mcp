module;

#include <QCoreApplication>
#include <QImage>
#include <QTimer>

export module ApiEditorMethods;

import std;
import ApiCommon;
import ApiError;
import ApiHost;
import Camera;
import Hierarchy;
import Map;
import Terrain;
import Texture;
import WorldUndoManager;
import "brush/brush.h";
import <glm/glm.hpp>;
import <nlohmann/json.hpp>;

namespace api {
	nlohmann::json ping(const nlohmann::json&) {
		return "pong";
	}

	nlohmann::json editor_version(const nlohmann::json&) {
		return {
			{"hivewe", "0.11"},
			{"api_version", version},
			{"warcraft_directory", hierarchy.warcraft_directory.string()},
			{"graphics", hierarchy.definitive ? "DE" : hierarchy.remastered ? "HD" : "SD"},
			{"ptr", hierarchy.ptr},
			{"locale", hierarchy.locale},
		};
	}

	/// Exits without the "are you sure" prompt. Unsaved changes are lost.
	nlohmann::json editor_quit(const nlohmann::json&) {
		// Later, so this response still goes out
		QTimer::singleShot(200, [] {
			QCoreApplication::exit(0);
		});
		return "quitting";
	}

	nlohmann::json edit_undo(const nlohmann::json&) {
		Map& m = loaded_map();
		if (m.brush) {
			m.brush->clear_selection();
		}
		auto context = m.edit_context();
		m.world_undo.undo(context);
		return "ok";
	}

	nlohmann::json edit_redo(const nlohmann::json&) {
		Map& m = loaded_map();
		if (m.brush) {
			m.brush->clear_selection();
		}
		auto context = m.edit_context();
		m.world_undo.redo(context);
		return "ok";
	}

	nlohmann::json camera_state(const Map& m) {
		const glm::vec2 world = internal_to_world(m, glm::vec2(camera.position));
		return {
			{"x", world.x},
			{"y", world.y},
			// Internally in tiles, the API uses game units like everything else
			{"distance", camera.distance * 128.f},
			{"yaw", glm::degrees(camera.horizontal_angle)},
			{"pitch", glm::degrees(camera.vertical_angle)},
		};
	}

	nlohmann::json camera_get(const nlohmann::json&) {
		return camera_state(loaded_map());
	}

	/// Moves the editor camera. Every parameter is optional.
	nlohmann::json camera_set(const nlohmann::json& params) {
		const Map& m = loaded_map();
		if (params.contains("x") || params.contains("y")) {
			const glm::vec2 current = internal_to_world(m, glm::vec2(camera.position));
			const glm::vec2 world(params.value("x", current.x), params.value("y", current.y));
			const glm::vec2 internal = world_to_internal(m, world);
			camera.position.x = internal.x;
			camera.position.y = internal.y;
		}
		if (params.contains("distance")) {
			camera.distance = std::clamp(params["distance"].get<float>() / 128.f, 0.001f, 1000.f);
		}
		if (params.contains("yaw")) {
			camera.horizontal_angle = glm::radians(params["yaw"].get<float>());
		}
		if (params.contains("pitch")) {
			const float limit = glm::pi<float>() / 2.f - 0.001f;
			camera.vertical_angle = std::clamp(glm::radians(params["pitch"].get<float>()), -limit, limit);
		}
		camera.update(0.0);
		return camera_state(m);
	}

	/// Renders the map viewport, optionally from a new camera position first
	nlohmann::json view_screenshot(const nlohmann::json& params) {
		loaded_map();
		nlohmann::json camera_params = params;
		camera_params.erase("max_size");
		if (!camera_params.empty()) {
			camera_set(camera_params);
		}
		return image_result(host.capture_viewport(), params.value("max_size", 1024));
	}

	/// The terrain minimap: tile colors, cliffs and water, no doodads or units
	nlohmann::json view_minimap(const nlohmann::json& params) {
		const Map& m = loaded_map();
		const auto texture = m.terrain.minimap_image();
		const QImage image(texture.data.data(), texture.width, texture.height, texture.width * 4, QImage::Format_RGBA8888);
		return image_result(image.copy(), params.value("max_size", 512));
	}

	export std::vector<MethodEntry> editor_methods() {
		return {
			{"ping", ping},
			{"editor.version", editor_version},
			{"editor.quit", editor_quit},
			{"edit.undo", edit_undo},
			{"edit.redo", edit_redo},
			{"camera.get", camera_get},
			{"camera.set", camera_set},
			{"view.screenshot", view_screenshot},
			{"view.minimap", view_minimap},
		};
	}
} // namespace api
