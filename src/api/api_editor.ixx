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

	/// Renders the map offscreen at width x height with the camera's aspect ratio matched, then restores it
	QImage render_offscreen(const int width, const int height) {
		const float aspect_ratio = camera.aspect_ratio;
		camera.aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
		camera.update(0.0);
		QImage image = host.capture_offscreen(width, height);
		camera.aspect_ratio = aspect_ratio;
		camera.update(0.0);
		return image;
	}

	/// Renders the map, optionally from a new camera position first. Rendered offscreen, so it works while the window is minimized.
	nlohmann::json view_screenshot(const nlohmann::json& params) {
		loaded_map();
		nlohmann::json camera_params = params;
		for (const char* key : {"max_size", "width", "height"}) {
			camera_params.erase(key);
		}
		if (!camera_params.empty()) {
			camera_set(camera_params);
		}
		const int width = std::clamp(params.value("width", 1280), 64, 4096);
		const int height = std::clamp(params.value("height", 720), 64, 4096);
		return image_result(render_offscreen(width, height), params.value("max_size", 1024));
	}

	/// The whole map from straight above, with doodads and units, then the camera goes back where it was
	nlohmann::json view_overview(const nlohmann::json& params) {
		const Map& m = loaded_map();
		const glm::vec3 position = camera.position;
		const float distance = camera.distance;
		const float horizontal_angle = camera.horizontal_angle;
		const float vertical_angle = camera.vertical_angle;

		const float size = static_cast<float>(std::max(m.terrain.width, m.terrain.height));
		camera.position = glm::vec3(m.terrain.width / 2.f, m.terrain.height / 2.f, 0.f);
		camera.horizontal_angle = 0.f;
		camera.vertical_angle = -glm::pi<float>() / 2.f + 0.001f;
		// Far enough that the whole map fits the 50 degree field of view
		camera.distance = size * 0.5f / std::tan(glm::radians(camera.fov) * 0.5f) * 1.04f;
		const int resolution = std::clamp(params.value("resolution", 1536), 256, 4096);
		QImage image = render_offscreen(resolution, resolution);

		camera.position = position;
		camera.distance = distance;
		camera.horizontal_angle = horizontal_angle;
		camera.vertical_angle = vertical_angle;
		camera.update(0.0);
		return image_result(image, params.value("max_size", 1024));
	}

	/// Which overlays and object kinds the view draws
	nlohmann::json view_state(const Map& m) {
		return {
			{"units", m.render_units},
			{"doodads", m.render_doodads},
			{"pathing", m.render_pathing},
			{"regions", m.render_regions},
			{"brush", m.render_brush},
			{"lighting", m.render_lighting},
			{"water", m.render_water},
			{"click_helpers", m.render_click_helpers},
			{"wireframe", m.render_wireframe},
		};
	}

	nlohmann::json view_get(const nlohmann::json&) {
		return view_state(loaded_map());
	}

	nlohmann::json view_set(const nlohmann::json& params) {
		Map& m = loaded_map();
		const std::pair<const char*, bool Map::*> toggles[] = {
			{"units", &Map::render_units},
			{"doodads", &Map::render_doodads},
			{"pathing", &Map::render_pathing},
			{"regions", &Map::render_regions},
			{"brush", &Map::render_brush},
			{"lighting", &Map::render_lighting},
			{"water", &Map::render_water},
			{"click_helpers", &Map::render_click_helpers},
			{"wireframe", &Map::render_wireframe},
		};
		for (const auto& [name, field] : toggles) {
			if (params.contains(name)) {
				m.*field = params[name].get<bool>();
			}
		}
		return view_state(m);
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
			{"view.overview", view_overview},
			{"view.get", view_get},
			{"view.set", view_set},
		};
	}
} // namespace api
