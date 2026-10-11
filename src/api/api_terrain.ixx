module;

#include <QRect>

export module ApiTerrainMethods;

import std;
import ApiCommon;
import ApiError;
import Camera;
import Doodads;
import Map;
import MapInfo;
import PathingMap;
import PathingUndo;
import Rects;
import Terrain;
import Tileset;
import Units;
import WorldUndoManager;
import "brush/terrain_brush.h";
import <glm/glm.hpp>;
import <nlohmann/json.hpp>;

namespace api {
	/// The API's own terrain brush, so strokes behave exactly like the editor's: cliffs, ramps, pathing, doodad heights and undo
	struct ApiTerrainBrush {
		const Map* owner = nullptr;
		std::unique_ptr<TerrainBrush> brush;
	};

	ApiTerrainBrush api_brush;

	TerrainBrush& terrain_brush(Map& m) {
		if (api_brush.owner != &m || !api_brush.brush) {
			api_brush.brush = std::make_unique<TerrainBrush>(m.terrain, m.units, m.tilesets, m.world_undo);
			api_brush.owner = &m;
		}
		return *api_brush.brush;
	}

	Brush::Shape parse_shape(const nlohmann::json& params) {
		const std::string shape = params.value("shape", "circle");
		if (shape == "circle") {
			return Brush::Shape::circle;
		}
		if (shape == "square") {
			return Brush::Shape::square;
		}
		if (shape == "diamond") {
			return Brush::Shape::diamond;
		}
		throw Error(error_code::invalid_params, "shape must be circle, square or diamond");
	}

	/// Activates one operator, positions the brush at params x/y with params size/shape and applies one stroke.
	/// `configure` runs after activation and before applying, to set operator options.
	template <typename Configure>
	nlohmann::json stroke(Map& m, TerrainOperator& op, const nlohmann::json& params, const double amount, Configure configure) {
		TerrainBrush& brush = terrain_brush(m);
		brush.deactivate_operator(brush.height_operator);
		brush.deactivate_operator(brush.texture_operator);
		brush.deactivate_operator(brush.cliff_operator);
		brush.deactivate_operator(brush.cell_operator);
		brush.activate_operator(op);

		const int size = params.value("size", 4);
		if (size < 1 || size > 250) {
			throw Error(error_code::invalid_params, "size must be between 1 and 250 tiles");
		}
		brush.set_size(glm::ivec2(size));
		brush.set_shape(parse_shape(params));
		configure(brush);

		const glm::vec2 center = world_to_internal(m, glm::vec2(required<float>(params, "x"), required<float>(params, "y")));
		const glm::vec3 saved_mouse = input_handler.mouse_world;
		input_handler.mouse_world = glm::vec3(center, 0.f);

		auto context = m.edit_context();
		brush.apply_begin(context);
		brush.apply(context, amount);
		brush.apply_end(context);

		input_handler.mouse_world = saved_mouse;
		return "ok";
	}

	nlohmann::json terrain_raise(const nlohmann::json& params) {
		Map& m = loaded_map();
		const float amount = required<float>(params, "amount");
		TerrainBrush& brush = terrain_brush(m);
		brush.height_operator.deformation_type = amount >= 0 ? HeightOperator::deformation::raise : HeightOperator::deformation::lower;
		// The height operator raises the brush center by frame_delta tiles, falling off towards the edge
		return stroke(m, brush.height_operator, params, std::abs(amount) / 128.0, [&](TerrainBrush& b) {
			b.deform_ground = true;
			b.deform_water = params.value("water", false);
		});
	}

	nlohmann::json terrain_smooth(const nlohmann::json& params) {
		Map& m = loaded_map();
		TerrainBrush& brush = terrain_brush(m);
		brush.height_operator.deformation_type = HeightOperator::deformation::smooth;
		const int passes = std::clamp(params.value("passes", 30), 1, 500);
		for (int i = 0; i < passes; i++) {
			stroke(m, brush.height_operator, params, 1.0, [&](TerrainBrush& b) {
				b.deform_ground = true;
				b.deform_water = false;
			});
		}
		return "ok";
	}

	nlohmann::json terrain_flatten(const nlohmann::json& params) {
		Map& m = loaded_map();
		TerrainBrush& brush = terrain_brush(m);
		brush.height_operator.deformation_type = HeightOperator::deformation::plateau;
		if (params.contains("height")) {
			// Plateau flattens to the center corner's height, so set that first
			const glm::vec2 center = world_to_internal(m, glm::vec2(required<float>(params, "x"), required<float>(params, "y")));
			const int x = std::clamp(static_cast<int>(std::round(center.x)), 0, m.terrain.width - 1);
			const int y = std::clamp(static_cast<int>(std::round(center.y)), 0, m.terrain.height - 1);
			const float height = std::clamp(params["height"].get<float>() / 128.f, Terrain::min_ground_height, Terrain::max_ground_height);
			m.terrain.corner_height[m.terrain.ci(x, y)] = height;
		}
		return stroke(m, brush.height_operator, params, 1.0, [&](TerrainBrush& b) {
			b.deform_ground = true;
			b.deform_water = false;
		});
	}

	nlohmann::json terrain_paint(const nlohmann::json& params) {
		Map& m = loaded_map();
		const std::string tile = required<std::string>(params, "tile");
		if (std::ranges::find(m.terrain.tileset_ids, tile) == m.terrain.tileset_ids.end()) {
			throw Error(error_code::invalid_params, std::format("Tile {} is not in this map's tiles: {}. Add it with terrain.set_tiles", tile, nlohmann::json(m.terrain.tileset_ids).dump()));
		}
		TerrainBrush& brush = terrain_brush(m);
		brush.texture_operator.tile_id = tile;
		return stroke(m, brush.texture_operator, params, 1.0, [](TerrainBrush&) {});
	}

	nlohmann::json terrain_cliff(const nlohmann::json& params) {
		Map& m = loaded_map();
		static const std::unordered_map<std::string, CliffOperator::cliff_operation> operations = {
			{"lower2", CliffOperator::cliff_operation::lower2},
			{"lower", CliffOperator::cliff_operation::lower1},
			{"level", CliffOperator::cliff_operation::level},
			{"raise", CliffOperator::cliff_operation::raise1},
			{"raise2", CliffOperator::cliff_operation::raise2},
			{"deep_water", CliffOperator::cliff_operation::deep_water},
			{"shallow_water", CliffOperator::cliff_operation::shallow_water},
			{"ramp", CliffOperator::cliff_operation::ramp},
		};
		const auto operation = operations.find(required<std::string>(params, "operation"));
		if (operation == operations.end()) {
			throw Error(error_code::invalid_params, "operation must be raise, lower, raise2, lower2, level, deep_water, shallow_water or ramp");
		}
		if (m.terrain.cliffset_ids.empty()) {
			throw Error(error_code::invalid_params, "This map's tiles have no cliff types");
		}
		const std::string cliff = params.value("cliff", m.terrain.cliffset_ids.front());
		if (std::ranges::find(m.terrain.cliffset_ids, cliff) == m.terrain.cliffset_ids.end()) {
			throw Error(error_code::invalid_params, std::format("Cliff {} is not in this map's cliffs: {}", cliff, nlohmann::json(m.terrain.cliffset_ids).dump()));
		}
		TerrainBrush& brush = terrain_brush(m);
		brush.cliff_operator.cliff_operation_type = operation->second;
		brush.cliff_operator.cliff_id = cliff;
		return stroke(m, brush.cliff_operator, params, 1.0, [](TerrainBrush&) {});
	}

	nlohmann::json terrain_cells(const nlohmann::json& params, const CellOperator::cell_operation operation) {
		Map& m = loaded_map();
		TerrainBrush& brush = terrain_brush(m);
		return stroke(m, brush.cell_operator, params, 1.0, [&](TerrainBrush& b) {
			b.cell_operator.set_operation_type(operation);
		});
	}

	nlohmann::json terrain_water(const nlohmann::json& params) {
		const std::string operation = required<std::string>(params, "operation");
		if (operation != "add" && operation != "remove") {
			throw Error(error_code::invalid_params, "operation must be add or remove");
		}
		return terrain_cells(params, operation == "add" ? CellOperator::cell_operation::add_water : CellOperator::cell_operation::remove_water);
	}

	nlohmann::json terrain_boundary(const nlohmann::json& params) {
		const std::string operation = required<std::string>(params, "operation");
		if (operation != "add" && operation != "remove") {
			throw Error(error_code::invalid_params, "operation must be add or remove");
		}
		return terrain_cells(params, operation == "add" ? CellOperator::cell_operation::add_boundary : CellOperator::cell_operation::remove_boundary);
	}

	/// Corner data in a rectangle of corners. x/y is the world position of the bottom left corner.
	nlohmann::json terrain_get(const nlohmann::json& params) {
		const Map& m = loaded_map();
		const Terrain& t = m.terrain;
		const glm::vec2 start = world_to_internal(m, glm::vec2(required<float>(params, "x"), required<float>(params, "y")));
		const int x0 = std::clamp(static_cast<int>(std::round(start.x)), 0, t.width - 1);
		const int y0 = std::clamp(static_cast<int>(std::round(start.y)), 0, t.height - 1);
		const int columns = std::clamp(params.value("columns", 1), 1, 64);
		const int rows = std::clamp(params.value("rows", 1), 1, 64);
		const int x1 = std::min(x0 + columns, t.width);
		const int y1 = std::min(y0 + rows, t.height);

		nlohmann::json corners = nlohmann::json::array();
		for (int j = y0; j < y1; j++) {
			for (int i = x0; i < x1; i++) {
				const Corner c = t.get_corner(i, j);
				const glm::vec2 world = internal_to_world(m, glm::vec2(i, j));
				nlohmann::json corner = {
					{"x", world.x},
					{"y", world.y},
					// The height the game uses: ground height plus cliff level, in game units
					{"height", t.corner_final_ground_height(i, j) * 128.f},
					{"cliff_level", c.layer_height},
					{"tile", c.ground_texture < t.tileset_ids.size() ? t.tileset_ids[c.ground_texture] : ""},
				};
				if (c.water) {
					corner["water_height"] = t.corner_final_water_height(i, j) * 128.f;
				}
				for (const auto& [name, flag] : {std::pair {"ramp", c.ramp}, {"blight", c.blight}, {"boundary", c.boundary}, {"cliff", c.cliff}, {"map_edge", c.map_edge}}) {
					if (flag) {
						corner[name] = true;
					}
				}
				corners.push_back(std::move(corner));
			}
		}
		return {
			{"columns", x1 - x0},
			{"rows", y1 - y0},
			{"corners", corners},
		};
	}

	nlohmann::json terrain_height_at(const nlohmann::json& params) {
		const Map& m = loaded_map();
		const glm::vec2 position = world_to_internal(m, glm::vec2(required<float>(params, "x"), required<float>(params, "y")));
		return {
			{"ground", m.terrain.interpolated_height(position.x, position.y, false) * 128.f},
			{"ground_or_water", m.terrain.interpolated_height(position.x, position.y, true) * 128.f},
		};
	}

	/// The tileset and tiles the map uses, plus every tile and tileset that exists
	nlohmann::json terrain_tiles(const nlohmann::json& params) {
		const Map& m = loaded_map();
		const TilesetData& tilesets = m.tilesets;

		nlohmann::json tiles = nlohmann::json::array();
		for (const auto& id : m.terrain.tileset_ids) {
			const TerrainTexture* texture = tilesets.terrain_texture(id);
			tiles.push_back({{"id", id}, {"name", texture ? texture->name : ""}});
		}
		nlohmann::json result = {
			{"tileset", std::string(1, m.terrain.tileset_id)},
			{"tiles", tiles},
			{"cliffs", m.terrain.cliffset_ids},
		};

		if (params.value("all", false)) {
			nlohmann::json all_tilesets = nlohmann::json::array();
			for (const auto& [id, tileset] : tilesets.tilesets()) {
				all_tilesets.push_back({{"id", std::string(1, id)}, {"name", tileset.name}, {"tiles", tileset.terrain_textures}});
			}
			nlohmann::json all_tiles = nlohmann::json::object();
			for (const auto& [id, texture] : tilesets.terrain_textures()) {
				all_tiles[id] = texture.name;
			}
			result["all_tilesets"] = all_tilesets;
			result["all_tiles"] = all_tiles;
		}
		return result;
	}

	/// Replaces the map's tile list (at most 16) and optionally its base tileset.
	/// Ground using a removed tile switches to "replace"[old] or the first new tile.
	nlohmann::json terrain_set_tiles(const nlohmann::json& params) {
		Map& m = loaded_map();
		const std::vector<std::string> new_tiles = required<std::vector<std::string>>(params, "tiles");
		if (new_tiles.empty() || new_tiles.size() > 16) {
			throw Error(error_code::invalid_params, "A map uses between 1 and 16 tiles");
		}
		for (const auto& id : new_tiles) {
			if (!m.tilesets.terrain_texture(id)) {
				throw Error(error_code::invalid_params, "Unknown tile " + id + ", see terrain.tiles with all=true");
			}
		}

		char tileset = m.terrain.tileset_id;
		if (params.contains("tileset")) {
			const std::string value = params["tileset"].get<std::string>();
			if (value.size() != 1 || !m.tilesets.tileset(value[0])) {
				throw Error(error_code::invalid_params, "Unknown tileset " + value);
			}
			tileset = value[0];
		}

		const nlohmann::json replace = params.value("replace", nlohmann::json::object());
		std::vector<int> old_to_new(m.terrain.tileset_ids.size());
		for (size_t i = 0; i < m.terrain.tileset_ids.size(); i++) {
			const std::string& old_id = m.terrain.tileset_ids[i];
			const std::string target = replace.contains(old_id) ? replace[old_id].get<std::string>() : old_id;
			const auto found = std::ranges::find(new_tiles, target);
			old_to_new[i] = found == new_tiles.end() ? 0 : static_cast<int>(found - new_tiles.begin());
		}

		m.terrain.change_tileset(new_tiles, old_to_new, tileset, m.tilesets, m.info);
		emit m.terrain.tileset_changed();
		return terrain_tiles(nlohmann::json::object());
	}

	/// Sets pathing flags of the cells (4 per tile) in a square or circle. Undoable.
	nlohmann::json pathing_set(const nlohmann::json& params) {
		Map& m = loaded_map();
		PathingMap& pathing = m.pathing_map;
		const glm::vec2 center = world_to_internal(m, glm::vec2(required<float>(params, "x"), required<float>(params, "y"))) * 4.f;
		const float radius = params.value("size", 1.f) * 4.f / 2.f;
		const bool circle = params.value("shape", "square") == "circle";

		uint8_t set = 0;
		uint8_t clear = 0;
		for (const auto& [name, flag] : {std::pair {"walkable", PathingMap::Flags::unwalkable}, {"flyable", PathingMap::Flags::unflyable}, {"buildable", PathingMap::Flags::unbuildable}}) {
			if (params.contains(name)) {
				// The flags say what is NOT allowed
				(params[name].get<bool>() ? clear : set) |= flag;
			}
		}
		if (params.contains("blight")) {
			(params["blight"].get<bool>() ? set : clear) |= PathingMap::Flags::blight;
		}
		if (!set && !clear) {
			throw Error(error_code::invalid_params, "Pass at least one of walkable, flyable, buildable, blight");
		}

		const PathingRect area = PathingRect(
									 static_cast<int>(std::floor(center.x - radius)),
									 static_cast<int>(std::floor(center.y - radius)),
									 static_cast<int>(std::ceil(radius * 2)),
									 static_cast<int>(std::ceil(radius * 2))
		)
									 .intersected(QRect(0, 0, pathing.width, pathing.height));
		if (area.isEmpty()) {
			throw Error(error_code::invalid_params, "The area is outside the map");
		}

		auto undo = std::make_unique<PathingMapAction>();
		undo->area = area;
		for (int j = area.top(); j <= area.bottom(); j++) {
			for (int i = area.left(); i <= area.right(); i++) {
				undo->old_pathing.push_back(pathing.pathing_cells_static[j * pathing.width + i]);
			}
		}
		int changed = 0;
		for (int j = area.top(); j <= area.bottom(); j++) {
			for (int i = area.left(); i <= area.right(); i++) {
				if (circle && glm::distance(glm::vec2(i + 0.5f, j + 0.5f), center) > radius) {
					continue;
				}
				uint8_t& cell = pathing.pathing_cells_static[j * pathing.width + i];
				cell = (cell & ~clear) | set;
				changed++;
			}
		}
		for (int j = area.top(); j <= area.bottom(); j++) {
			for (int i = area.left(); i <= area.right(); i++) {
				undo->new_pathing.push_back(pathing.pathing_cells_static[j * pathing.width + i]);
			}
		}
		pathing.upload_static_pathing();
		m.world_undo.new_undo_group();
		m.world_undo.add_undo_action(std::move(undo));
		return {{"cells_changed", changed}};
	}

	/// The combined (terrain + doodad) pathing of the cells around a point
	nlohmann::json pathing_get(const nlohmann::json& params) {
		const Map& m = loaded_map();
		const PathingMap& pathing = m.pathing_map;
		const glm::ivec2 cell = glm::ivec2(world_to_internal(m, glm::vec2(required<float>(params, "x"), required<float>(params, "y"))) * 4.f);
		if (cell.x < 0 || cell.y < 0 || cell.x >= pathing.width || cell.y >= pathing.height) {
			throw Error(error_code::invalid_params, "The point is outside the map");
		}
		const uint8_t flags = pathing.pathing_cells_static[cell.y * pathing.width + cell.x] | pathing.pathing_cells_dynamic[cell.y * pathing.width + cell.x];
		return {
			{"walkable", !(flags & PathingMap::Flags::unwalkable)},
			{"flyable", !(flags & PathingMap::Flags::unflyable)},
			{"buildable", !(flags & PathingMap::Flags::unbuildable)},
			{"blight", (flags & PathingMap::Flags::blight) != 0},
			{"water", (flags & PathingMap::Flags::water) != 0},
		};
	}

	export std::vector<MethodEntry> terrain_methods() {
		return {
			{"terrain.raise", terrain_raise},
			{"terrain.smooth", terrain_smooth},
			{"terrain.flatten", terrain_flatten},
			{"terrain.paint", terrain_paint},
			{"terrain.cliff", terrain_cliff},
			{"terrain.water", terrain_water},
			{"terrain.boundary", terrain_boundary},
			{"terrain.get", terrain_get},
			{"terrain.height_at", terrain_height_at},
			{"terrain.tiles", terrain_tiles},
			{"terrain.set_tiles", terrain_set_tiles},
			{"pathing.set", pathing_set},
			{"pathing.get", pathing_get},
		};
	}
} // namespace api
