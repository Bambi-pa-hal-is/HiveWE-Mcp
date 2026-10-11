export module ApiMapInfoMethods;

import std;
import ApiCommon;
import ApiError;
import Map;
import MapInfo;
import TriggerStrings;
import Units;
import Utilities;
import <glm/glm.hpp>;
import <nlohmann/json.hpp>;

namespace api {
	/// Texts in the w3i are usually TRIGSTR_ references into war3map.wts, set the text behind them
	void set_text(Map& m, std::string& field, const std::string& value) {
		if (!field.starts_with("TRIGSTR_")) {
			field.clear();
		}
		m.trigger_strings.set_string(field, value);
	}

	std::string four_cc_to_string(const uint32_t value) {
		if (value == 0) {
			return "";
		}
		std::string result(4, '\0');
		std::memcpy(result.data(), &value, 4);
		return result;
	}

	uint32_t string_to_four_cc(const std::string& value) {
		if (value.empty()) {
			return 0;
		}
		if (value.size() != 4) {
			throw Error(error_code::invalid_params, "Expected a 4 character id: " + value);
		}
		uint32_t result;
		std::memcpy(&result, value.data(), 4);
		return result;
	}

	nlohmann::json color_json(const glm::u8vec4 color) {
		return {color.r, color.g, color.b, color.a};
	}

	glm::u8vec4 parse_color(const nlohmann::json& value) {
		return {value[0].get<int>(), value[1].get<int>(), value[2].get<int>(), value.size() > 3 ? value[3].get<int>() : 255};
	}

	/// The boolean map options, by API name
	std::vector<std::pair<const char*, bool MapInfo::*>> option_fields() {
		return {
			{"hide_minimap_preview", &MapInfo::hide_minimap_preview},
			{"modify_ally_priorities", &MapInfo::modif_ally_priorities},
			{"melee_map", &MapInfo::melee_map},
			{"masked_area_partially_visible", &MapInfo::masked_area_partially_visible},
			{"fixed_player_settings", &MapInfo::fixed_player_settings},
			{"custom_forces", &MapInfo::custom_forces},
			{"custom_techtree", &MapInfo::custom_techtree},
			{"custom_abilities", &MapInfo::custom_abilities},
			{"custom_upgrades", &MapInfo::custom_upgrades},
			{"cliff_shore_waves", &MapInfo::cliff_shore_waves},
			{"rolling_shore_waves", &MapInfo::rolling_shore_waves},
			{"use_terrain_fog", &MapInfo::use_terrain_fog},
			{"item_classification", &MapInfo::item_classification},
			{"water_tinting", &MapInfo::water_tinting},
			{"accurate_probability", &MapInfo::accurate_probability_for_calculations},
			{"custom_ability_skins", &MapInfo::custom_ability_skins},
			{"disable_deny_icon", &MapInfo::disable_deny_icon},
			{"force_default_zoom", &MapInfo::force_default_zoom},
			{"force_max_zoom", &MapInfo::force_max_zoom},
			{"force_min_zoom", &MapInfo::force_min_zoom},
			{"override_hd_water_color", &MapInfo::override_hd_water_color},
			{"dynamic_minimap", &MapInfo::dynamic_minimap},
		};
	}

	/// The w3i texts, by API name
	std::vector<std::pair<const char*, std::string MapInfo::*>> text_fields() {
		return {
			{"name", &MapInfo::name},
			{"author", &MapInfo::author},
			{"description", &MapInfo::description},
			{"suggested_players", &MapInfo::suggested_players},
			{"loading_screen_text", &MapInfo::loading_screen_text},
			{"loading_screen_title", &MapInfo::loading_screen_title},
			{"loading_screen_subtitle", &MapInfo::loading_screen_subtitle},
			{"prologue_text", &MapInfo::prologue_text},
			{"prologue_title", &MapInfo::prologue_title},
			{"prologue_subtitle", &MapInfo::prologue_subtitle},
		};
	}

	nlohmann::json mapinfo_get(const nlohmann::json&) {
		const Map& m = loaded_map();
		const MapInfo& info = m.info;

		nlohmann::json result;
		for (const auto& [name, field] : text_fields()) {
			result[name] = resolve_string(m, info.*field);
		}
		nlohmann::json options;
		for (const auto& [name, field] : option_fields()) {
			options[name] = info.*field;
		}
		result["options"] = options;
		result["script_language"] = info.lua ? "lua" : "jass";
		result["loading_screen_model"] = info.loading_screen_model;
		result["loading_screen_number"] = info.loading_screen_number;
		result["prologue_screen_model"] = info.prologue_screen_model;
		result["game_data_set"] = info.game_data_set;
		result["fog"] = {
			{"style", info.fog_style},
			{"start_z", info.fog_start_z_height},
			{"end_z", info.fog_end_z_height},
			{"density", info.fog_density},
			{"color", color_json(info.fog_color)},
		};
		result["weather"] = four_cc_to_string(static_cast<uint32_t>(info.weather_id));
		result["sound_environment"] = info.custom_sound_environment;
		result["light_tileset"] = info.custom_light_tileset ? std::string(1, info.custom_light_tileset) : "";
		result["ambience_tileset"] = info.custom_ambience_tileset ? std::string(1, info.custom_ambience_tileset) : "";
		result["water_color"] = color_json(info.water_color);
		result["camera_distance"] = {
			{"default", info.default_cam_distance},
			{"min", info.min_cam_distance},
			{"max", info.max_cam_distance},
		};
		result["playable_width"] = info.playable_width;
		result["playable_height"] = info.playable_height;
		return result;
	}

	nlohmann::json mapinfo_set(const nlohmann::json& params) {
		Map& m = loaded_map();
		MapInfo& info = m.info;

		for (const auto& [name, field] : text_fields()) {
			if (params.contains(name)) {
				set_text(m, info.*field, params[name].get<std::string>());
			}
		}
		if (params.contains("options")) {
			const auto fields = option_fields();
			for (const auto& [name, value] : params["options"].items()) {
				const auto found = std::ranges::find_if(fields, [&](const auto& field) {
					return field.first == name;
				});
				if (found == fields.end()) {
					throw Error(error_code::invalid_params, "Unknown option " + name);
				}
				info.*(found->second) = value.get<bool>();
			}
		}
		if (params.contains("script_language")) {
			const std::string language = params["script_language"].get<std::string>();
			if (language != "jass" && language != "lua") {
				throw Error(error_code::invalid_params, "script_language must be jass or lua");
			}
			info.lua = language == "lua";
		}
		if (params.contains("loading_screen_model")) {
			info.loading_screen_model = params["loading_screen_model"].get<std::string>();
		}
		if (params.contains("loading_screen_number")) {
			info.loading_screen_number = params["loading_screen_number"].get<int>();
		}
		if (params.contains("prologue_screen_model")) {
			info.prologue_screen_model = params["prologue_screen_model"].get<std::string>();
		}
		if (params.contains("game_data_set")) {
			info.game_data_set = params["game_data_set"].get<int>();
		}
		if (params.contains("fog")) {
			const nlohmann::json& fog = params["fog"];
			info.fog_style = fog.value("style", info.fog_style);
			info.fog_start_z_height = fog.value("start_z", info.fog_start_z_height);
			info.fog_end_z_height = fog.value("end_z", info.fog_end_z_height);
			info.fog_density = fog.value("density", info.fog_density);
			if (fog.contains("color")) {
				info.fog_color = parse_color(fog["color"]);
			}
		}
		if (params.contains("weather")) {
			info.weather_id = static_cast<int>(string_to_four_cc(params["weather"].get<std::string>()));
		}
		if (params.contains("sound_environment")) {
			info.custom_sound_environment = params["sound_environment"].get<std::string>();
		}
		for (const auto& [name, field] : {std::pair {"light_tileset", &MapInfo::custom_light_tileset}, {"ambience_tileset", &MapInfo::custom_ambience_tileset}}) {
			if (params.contains(name)) {
				const std::string value = params[name].get<std::string>();
				if (value.size() > 1 || (value.size() == 1 && !m.tilesets.tileset(value[0]))) {
					throw Error(error_code::invalid_params, std::format("{} must be a tileset letter or empty", name));
				}
				info.*field = value.empty() ? 0 : value[0];
			}
		}
		if (params.contains("water_color")) {
			info.water_color = parse_color(params["water_color"]);
		}
		if (params.contains("camera_distance")) {
			const nlohmann::json& distance = params["camera_distance"];
			info.default_cam_distance = distance.value("default", info.default_cam_distance);
			info.min_cam_distance = distance.value("min", info.min_cam_distance);
			info.max_cam_distance = distance.value("max", info.max_cam_distance);
		}
		return mapinfo_get(nlohmann::json::object());
	}

	// ---------------------------------------------------------------- Players

	constexpr std::array controllers = {"human", "computer", "neutral", "rescuable"};
	constexpr std::array races = {"selectable", "human", "orc", "undead", "night_elf"};

	std::vector<int> mask_to_players(const uint32_t mask) {
		std::vector<int> players;
		for (int i = 0; i < 32; i++) {
			if (mask & (1u << i)) {
				players.push_back(i);
			}
		}
		return players;
	}

	uint32_t players_to_mask(const nlohmann::json& players) {
		uint32_t mask = 0;
		for (const int player : players.get<std::vector<int>>()) {
			if (player < 0 || player > 23) {
				throw Error(error_code::invalid_params, "Player numbers are 0 to 23");
			}
			mask |= 1u << player;
		}
		return mask;
	}

	Unit* start_location(Map& m, const int player) {
		for (auto& unit : m.units.units) {
			if (unit.id == "sloc" && unit.player == player) {
				return &unit;
			}
		}
		return nullptr;
	}

	nlohmann::json player_json(Map& m, const auto& player) {
		nlohmann::json result = {
			{"number", player.internal_number},
			{"name", resolve_string(m, player.name)},
			{"controller", controllers[static_cast<int>(player.type)]},
			{"race", races[static_cast<int>(player.race)]},
			{"fixed_start", player.fixed_start_position != 0},
			{"ally_low_priority", mask_to_players(player.ally_low_priorities_flags)},
			{"ally_high_priority", mask_to_players(player.ally_high_priorities_flags)},
		};
		if (const Unit* location = start_location(m, player.internal_number)) {
			const glm::vec2 world = internal_to_world(m, glm::vec2(location->position));
			result["start_x"] = world.x;
			result["start_y"] = world.y;
		}
		return result;
	}

	nlohmann::json players_list(const nlohmann::json&) {
		Map& m = loaded_map();
		nlohmann::json result = nlohmann::json::array();
		for (const auto& player : m.info.players) {
			result.push_back(player_json(m, player));
		}
		return result;
	}

	/// Creates or updates the player with this number. Start locations are the "sloc" markers the game reads.
	nlohmann::json players_set(const nlohmann::json& params) {
		Map& m = loaded_map();
		const int number = required<int>(params, "number");
		if (number < 0 || number > 23) {
			throw Error(error_code::invalid_params, "Player numbers are 0 to 23");
		}

		auto found = std::ranges::find_if(m.info.players, [&](const auto& player) {
			return player.internal_number == number;
		});
		if (found == m.info.players.end()) {
			auto& player = m.info.players.emplace_back();
			player.internal_number = number;
			player.type = PlayerType::human;
			player.race = PlayerRace::human;
			player.fixed_start_position = 0;
			player.starting_position = {0.f, 0.f};
			player.ally_low_priorities_flags = 0;
			player.ally_high_priorities_flags = 0;
			player.enemy_low_priorities_flags = 0;
			player.enemy_high_priorities_flags = 0;
			set_text(m, player.name, std::format("Player {}", number + 1));
			std::ranges::sort(m.info.players, {}, [](const auto& p) {
				return p.internal_number;
			});
			found = std::ranges::find_if(m.info.players, [&](const auto& p) {
				return p.internal_number == number;
			});
		}
		auto& player = *found;

		if (params.contains("name")) {
			set_text(m, player.name, params["name"].get<std::string>());
		}
		if (params.contains("controller")) {
			const auto value = std::ranges::find(controllers, params["controller"].get<std::string>());
			if (value == controllers.end()) {
				throw Error(error_code::invalid_params, "controller must be human, computer, neutral or rescuable");
			}
			player.type = static_cast<PlayerType>(value - controllers.begin());
		}
		if (params.contains("race")) {
			const auto value = std::ranges::find(races, params["race"].get<std::string>());
			if (value == races.end()) {
				throw Error(error_code::invalid_params, "race must be selectable, human, orc, undead or night_elf");
			}
			player.race = static_cast<PlayerRace>(value - races.begin());
		}
		if (params.contains("fixed_start")) {
			player.fixed_start_position = params["fixed_start"].get<bool>() ? 1 : 0;
		}
		if (params.contains("ally_low_priority")) {
			player.ally_low_priorities_flags = players_to_mask(params["ally_low_priority"]);
		}
		if (params.contains("ally_high_priority")) {
			player.ally_high_priorities_flags = players_to_mask(params["ally_high_priority"]);
		}
		if (params.contains("start_x") || params.contains("start_y")) {
			Unit* location = start_location(m, number);
			const glm::vec2 current = location ? internal_to_world(m, glm::vec2(location->position)) : glm::vec2(0.f);
			const glm::vec2 world(params.value("start_x", current.x), params.value("start_y", current.y));
			const glm::vec2 internal = world_to_internal(m, world);
			if (!location) {
				Unit marker;
				marker.id = "sloc";
				marker.skin_id = "sloc";
				marker.player = number;
				marker.scale = glm::vec3(1.f);
				marker.angle = glm::radians(270.f);
				marker.random = {1, 0, 0, 0};
				marker.creation_number = ++Unit::auto_increment;
				m.units.units.push_back(marker);
				location = &m.units.units.back();
			}
			location->position = glm::vec3(internal, m.terrain.interpolated_height(internal.x, internal.y, false));
			player.starting_position = world;
		}
		return player_json(m, player);
	}

	nlohmann::json players_delete(const nlohmann::json& params) {
		Map& m = loaded_map();
		const int number = required<int>(params, "number");
		const size_t removed = std::erase_if(m.info.players, [&](const auto& player) {
			return player.internal_number == number;
		});
		if (removed == 0) {
			throw Error(error_code::invalid_params, std::format("No player {}", number));
		}
		std::erase_if(m.units.units, [&](const Unit& unit) {
			return unit.id == "sloc" && unit.player == number;
		});
		return {{"deleted", number}};
	}

	// ---------------------------------------------------------------- Forces

	nlohmann::json forces_list(const nlohmann::json&) {
		Map& m = loaded_map();
		nlohmann::json forces = nlohmann::json::array();
		for (size_t i = 0; i < m.info.forces.size(); i++) {
			const auto& force = m.info.forces[i];
			forces.push_back({
				{"index", i},
				{"name", resolve_string(m, force.name)},
				{"players", mask_to_players(static_cast<uint32_t>(force.player_masks))},
				{"allied", force.allied},
				{"allied_victory", force.allied_victory},
				{"share_vision", force.share_vision},
				{"share_unit_control", force.share_unit_control},
				{"share_advanced_unit_control", force.share_advanced_unit_control},
			});
		}
		return {{"custom_forces", m.info.custom_forces}, {"forces", forces}};
	}

	/// Replaces all forces and turns on custom forces. A player can only be in one force.
	nlohmann::json forces_set(const nlohmann::json& params) {
		Map& m = loaded_map();
		const nlohmann::json list = required<nlohmann::json>(params, "forces");
		if (list.empty()) {
			throw Error(error_code::invalid_params, "A map needs at least one force");
		}

		decltype(m.info.forces) forces;
		uint32_t seen = 0;
		for (const auto& entry : list) {
			auto& force = forces.emplace_back();
			const uint32_t mask = players_to_mask(entry.value("players", nlohmann::json::array()));
			if (mask & seen) {
				throw Error(error_code::invalid_params, "A player is in more than one force");
			}
			seen |= mask;
			force.player_masks = static_cast<int>(mask);
			force.allied = entry.value("allied", false);
			force.allied_victory = entry.value("allied_victory", false);
			force.share_vision = entry.value("share_vision", false);
			force.share_unit_control = entry.value("share_unit_control", false);
			force.share_advanced_unit_control = entry.value("share_advanced_unit_control", false);
			force.name = "";
			set_text(m, force.name, entry.value("name", std::format("Force {}", forces.size())));
		}
		m.info.forces = std::move(forces);
		m.info.custom_forces = params.value("custom_forces", true);
		return forces_list(nlohmann::json::object());
	}

	// ---------------------------------------------------------------- Tech availability

	nlohmann::json tech_get(const nlohmann::json&) {
		const Map& m = loaded_map();
		nlohmann::json unavailable = nlohmann::json::array();
		for (const auto& tech : m.info.available_tech) {
			unavailable.push_back({{"id", tech.id}, {"players", mask_to_players(static_cast<uint32_t>(tech.player_flags))}});
		}
		nlohmann::json upgrades = nlohmann::json::array();
		for (const auto& upgrade : m.info.available_upgrades) {
			upgrades.push_back({
				{"id", upgrade.id},
				{"players", mask_to_players(static_cast<uint32_t>(upgrade.player_flags))},
				{"level", upgrade.level},
				{"availability", std::array {"unavailable", "available", "researched"}[std::clamp(upgrade.availability, 0, 2)]},
			});
		}
		return {{"unavailable", unavailable}, {"upgrades", upgrades}};
	}

	/// Replaces the tech lists that are given: "unavailable" units/items/abilities per player, and upgrade "upgrades" levels
	nlohmann::json tech_set(const nlohmann::json& params) {
		Map& m = loaded_map();
		if (params.contains("unavailable")) {
			decltype(m.info.available_tech) list;
			for (const auto& entry : params["unavailable"]) {
				auto& tech = list.emplace_back();
				tech.id = required<std::string>(entry, "id");
				tech.player_flags = static_cast<int>(entry.contains("players") ? players_to_mask(entry["players"]) : 0xFFFFFF);
			}
			m.info.available_tech = std::move(list);
			m.info.custom_techtree = !m.info.available_tech.empty();
			m.info.custom_abilities = m.info.custom_techtree;
		}
		if (params.contains("upgrades")) {
			decltype(m.info.available_upgrades) list;
			for (const auto& entry : params["upgrades"]) {
				auto& upgrade = list.emplace_back();
				upgrade.id = required<std::string>(entry, "id");
				upgrade.player_flags = static_cast<int>(entry.contains("players") ? players_to_mask(entry["players"]) : 0xFFFFFF);
				upgrade.level = entry.value("level", 1);
				const std::string availability = entry.value("availability", "unavailable");
				constexpr std::array names = {"unavailable", "available", "researched"};
				const auto found = std::ranges::find(names, availability);
				if (found == names.end()) {
					throw Error(error_code::invalid_params, "availability must be unavailable, available or researched");
				}
				upgrade.availability = static_cast<int>(found - names.begin());
			}
			m.info.available_upgrades = std::move(list);
			m.info.custom_upgrades = !m.info.available_upgrades.empty();
		}
		return tech_get(nlohmann::json::object());
	}

	// ---------------------------------------------------------------- Random tables

	nlohmann::json random_tables_get(const nlohmann::json&) {
		const Map& m = loaded_map();
		nlohmann::json unit_tables = nlohmann::json::array();
		for (const auto& table : m.info.random_unit_tables) {
			nlohmann::json lines = nlohmann::json::array();
			for (const auto& line : table.lines) {
				lines.push_back({{"chance", line.chance}, {"ids", line.ids}});
			}
			unit_tables.push_back({{"number", table.creation_number}, {"name", table.name}, {"positions", table.positions}, {"lines", lines}});
		}
		nlohmann::json item_tables = nlohmann::json::array();
		for (const auto& table : m.info.random_item_tables) {
			nlohmann::json sets = nlohmann::json::array();
			for (const auto& set : table.item_sets) {
				nlohmann::json items = nlohmann::json::array();
				for (const auto& [chance, id] : set.items) {
					items.push_back({{"type", id}, {"chance", chance}});
				}
				sets.push_back(items);
			}
			item_tables.push_back({{"number", table.creation_number}, {"name", table.name}, {"sets", sets}});
		}
		return {{"unit_tables", unit_tables}, {"item_tables", item_tables}};
	}

	/// Replaces the given lists. Item tables become ItemTable_<number> drop functions in the map script.
	nlohmann::json random_tables_set(const nlohmann::json& params) {
		Map& m = loaded_map();
		if (params.contains("item_tables")) {
			decltype(m.info.random_item_tables) list;
			int number = 0;
			for (const auto& entry : params["item_tables"]) {
				auto& table = list.emplace_back();
				table.creation_number = entry.value("number", number++);
				table.name = entry.value("name", std::format("Item Table {}", table.creation_number));
				for (const auto& set : entry.value("sets", nlohmann::json::array())) {
					ItemSet item_set;
					for (const auto& item : set) {
						item_set.items.emplace_back(item.value("chance", 100), required<std::string>(item, "type"));
					}
					table.item_sets.push_back(item_set);
				}
			}
			m.info.random_item_tables = std::move(list);
		}
		if (params.contains("unit_tables")) {
			decltype(m.info.random_unit_tables) list;
			int number = 0;
			for (const auto& entry : params["unit_tables"]) {
				auto& table = list.emplace_back();
				table.creation_number = entry.value("number", number++);
				table.name = entry.value("name", std::format("Unit Table {}", table.creation_number));
				table.positions = entry.value("positions", std::vector<int> {0});
				for (const auto& line : entry.value("lines", nlohmann::json::array())) {
					auto& table_line = table.lines.emplace_back();
					table_line.chance = line.value("chance", 100);
					table_line.ids = required<std::vector<std::string>>(line, "ids");
				}
			}
			m.info.random_unit_tables = std::move(list);
		}
		return random_tables_get(nlohmann::json::object());
	}

	export std::vector<MethodEntry> map_info_methods() {
		return {
			{"mapinfo.get", mapinfo_get},
			{"mapinfo.set", mapinfo_set},
			{"players.list", players_list},
			{"players.set", players_set},
			{"players.delete", players_delete},
			{"forces.list", forces_list},
			{"forces.set", forces_set},
			{"tech.get", tech_get},
			{"tech.set", tech_set},
			{"random_tables.get", random_tables_get},
			{"random_tables.set", random_tables_set},
		};
	}
} // namespace api
