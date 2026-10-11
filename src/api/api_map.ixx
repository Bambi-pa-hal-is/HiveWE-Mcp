module;

#include <QProcess>
#include <QSettings>
#include <QString>
#include <QStringList>

export module ApiMapMethods;

import std;
import ApiCommon;
import ApiError;
import ApiHost;
import Doodads;
import Hierarchy;
import Map;
import MapGlobal;
import MapInfo;
import MPQ;
import Terrain;
import TriggerStrings;
import <nlohmann/json.hpp>;

namespace fs = std::filesystem;

namespace api {
	void update_window_title(const Map& m) {
		const std::string location = m.is_in_temp_dir ? "Untitled Map" : m.filesystem_path.string();
		host.set_window_title(QString::fromStdString("HiveWE 0.11 - " + location));
	}

	/// Saves without dialogs: problems come back as an error or in the returned "warnings"
	nlohmann::json save_to(Map& m, const fs::path& directory) {
		host.saving_initiated();
		m.show_dialogs = false;
		const bool saved = m.save(directory);
		m.show_dialogs = true;
		if (!saved) {
			throw Error(error_code::internal_error, "Saving failed: " + (m.save_errors.empty() ? std::string("unknown error") : m.save_errors.front()));
		}
		return {
			{"path", m.filesystem_path.string()},
			{"warnings", m.save_errors},
		};
	}

	nlohmann::json map_info(const nlohmann::json&) {
		const Map& m = loaded_map();
		return {
			{"name", resolve_string(m, m.info.name)},
			{"author", resolve_string(m, m.info.author)},
			{"description", resolve_string(m, m.info.description)},
			{"suggested_players", resolve_string(m, m.info.suggested_players)},
			{"path", m.filesystem_path.string()},
			{"unsaved_new_map", m.is_in_temp_dir},
			{"tileset", std::string(1, m.terrain.tileset_id)},
			// The terrain is stored in corners, a map of N tiles has N + 1 corners
			{"width", m.terrain.width - 1},
			{"height", m.terrain.height - 1},
			{"playable_width", m.info.playable_width},
			{"playable_height", m.info.playable_height},
			// World coordinates of the bottom left map corner; the top right is offset + size * 128
			{"offset_x", m.terrain.offset.x},
			{"offset_y", m.terrain.offset.y},
			{"script_language", m.info.lua ? "lua" : "jass"},
			{"players", m.info.players.size()},
			{"forces", m.info.forces.size()},
			{"units", m.units.units.size()},
			{"items", m.units.items.size()},
			{"doodads", m.doodads.doodads.size()},
			{"regions", m.regions.regions.size()},
		};
	}

	/// Creates a flat Lordaeron Summer map like File > New, without asking to save the current one.
	/// It lives in a temporary folder until map.save is given a path.
	nlohmann::json map_new(const nlohmann::json& params) {
		const int width = params.value("width", 64);
		const int height = params.value("height", 64);
		for (const int size : {width, height}) {
			if (size < 32 || size > 480 || size % 32 != 0) {
				throw Error(error_code::invalid_params, "Map width and height must be multiples of 32 between 32 and 480");
			}
		}

		fs::path directory;
		do {
			directory = fs::temp_directory_path() / "HiveWE" / std::format("{:08x}", std::random_device {}()) / "New Map";
		} while (fs::exists(directory));
		fs::create_directories(directory);

		// The same minimal set of files HiveWE::new_map writes
		hierarchy.map_directory = directory;
		const float offset_x = -width * 128.f / 2.f;
		const float offset_y = -height * 128.f / 2.f;

		TriggerStrings trigger_strings;
		MapInfo info;
		info.load_defaults(trigger_strings);
		info.update_map_bounds_info(0, 0, 0, 0, width + 1, height + 1, offset_x, offset_y);
		info.save('L');
		trigger_strings.save();
		Terrain::save_blank(width, height);
		Doodads::save_empty();

		host.load_map(directory);
		map->is_in_temp_dir = true;
		update_window_title(*map);
		return map_info(nlohmann::json::object());
	}

	/// Opens a folder map, or unpacks a .w3x/.w3m archive into a folder and opens that
	nlohmann::json map_open(const nlohmann::json& params) {
		fs::path path = fs::absolute(required<std::string>(params, "path"));
		if (!fs::exists(path)) {
			throw Error(error_code::invalid_params, "No such file or folder: " + path.string());
		}

		if (fs::is_regular_file(path)) {
			const fs::path unpack_to = params.contains("unpack_to") ? fs::absolute(params["unpack_to"].get<std::string>())
																	: path.parent_path() / path.stem();
			if (fs::exists(unpack_to / "war3map.w3i")) {
				throw Error(error_code::invalid_params, "A folder map already exists at " + unpack_to.string() + ", open it or pass another \"unpack_to\"");
			}

			mpq::MPQ archive;
			if (!archive.open(path)) {
				throw Error(error_code::internal_error, "Could not open the archive, is it open in another program?");
			}
			fs::create_directories(unpack_to);
			if (!archive.unpack(unpack_to)) {
				throw Error(error_code::internal_error, "Unpacking the archive failed");
			}
			path = unpack_to;
		}

		// The files loading needs; checked first, as a failed load leaves no map open
		for (const char* required_file : {"war3map.w3i", "war3map.w3e", "war3map.doo"}) {
			if (!fs::exists(path / required_file)) {
				throw Error(error_code::invalid_params, std::format("Can't open {}: it has no {} (a protected map?)", path.string(), required_file));
			}
		}

		host.load_map(path);
		update_window_title(*map);
		return map_info(nlohmann::json::object());
	}

	/// Saves to the current folder, or to "path" (the map folder itself) when given
	nlohmann::json map_save(const nlohmann::json& params) {
		Map& m = loaded_map();
		if (!params.contains("path")) {
			if (m.is_in_temp_dir) {
				throw Error(error_code::invalid_params, "This new map has no folder yet, pass \"path\"");
			}
			return save_to(m, m.filesystem_path);
		}

		const fs::path path = fs::absolute(params["path"].get<std::string>());
		if (fs::exists(path) && !fs::equivalent(path, m.filesystem_path) && !fs::is_empty(path)) {
			throw Error(error_code::invalid_params, "The folder is not empty: " + path.string());
		}
		// Map::save copies the current folder into this one, and needs it to exist
		fs::create_directories(path);
		hierarchy.map_directory = path;
		nlohmann::json result = save_to(m, path);
		m.is_in_temp_dir = false;
		update_window_title(m);
		return result;
	}

	/// Saves and starts Warcraft III on the map, like the Test Map button
	nlohmann::json map_test(const nlohmann::json&) {
		Map& m = loaded_map();
		if (m.is_in_temp_dir) {
			throw Error(error_code::invalid_params, "Save this new map to a folder first with map.save");
		}
		nlohmann::json result = save_to(m, m.filesystem_path);

		const fs::path warcraft = hierarchy.root_directory / "x86_64" / "Warcraft III.exe";
		if (!fs::exists(warcraft)) {
			throw Error(error_code::internal_error, "Warcraft III.exe not found at " + warcraft.string());
		}
		QStringList arguments;
		arguments << "-launch" << "-loadfile" << QString::fromStdString(fs::canonical(m.filesystem_path).string());
		const QSettings settings;
		if (settings.value("testArgs").toString() != "") {
			arguments << settings.value("testArgs").toString().split(' ');
		}
		if (!QProcess::startDetached(QString::fromStdString(fs::canonical(warcraft).string()), arguments)) {
			throw Error(error_code::internal_error, "Starting Warcraft III failed");
		}
		result["launched"] = true;
		return result;
	}

	export std::vector<MethodEntry> map_methods() {
		return {
			{"map.info", map_info},
			{"map.new", map_new},
			{"map.open", map_open},
			{"map.save", map_save},
			{"map.test", map_test},
		};
	}
} // namespace api
