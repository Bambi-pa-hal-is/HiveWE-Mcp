module;

#define __STORMLIB_NO_STATIC_LINK__
#include <StormLib.h>

export module ApiAssetMethods;

import std;
import ApiCommon;
import ApiError;
import ApiHost;
import Hierarchy;
import Imports;
import Map;
import Protection;
import Sounds;
import <nlohmann/json.hpp>;

namespace fs = std::filesystem;

namespace api {
	/// A path inside the map folder, refusing anything that would land outside it
	fs::path map_relative(const Map& m, const std::string& relative) {
		const fs::path path = fs::path(relative).lexically_normal();
		if (relative.empty() || path.is_absolute() || path.begin()->string() == "..") {
			throw Error(error_code::invalid_params, "Import paths are relative to the map, e.g. war3mapImported/Model.mdx");
		}
		return m.filesystem_path / path;
	}

	nlohmann::json imports_list(const nlohmann::json& params) {
		const Map& m = loaded_map();
		const std::string extension = params.value("extension", "");
		nlohmann::json files = nlohmann::json::array();
		for (const auto& entry : fs::recursive_directory_iterator(m.filesystem_path)) {
			if (!entry.is_regular_file() || m.imports.blacklist.contains(entry.path().filename().string())) {
				continue;
			}
			const fs::path relative = entry.path().lexically_relative(m.filesystem_path);
			if (protection::is_editor_file(relative)) {
				continue;
			}
			if (!extension.empty() && entry.path().extension() != extension) {
				continue;
			}
			files.push_back({{"path", relative.generic_string()}, {"bytes", entry.file_size()}});
		}
		return files;
	}

	/// Copies a file from disk into the map. Models must be imported before units or doodads use them.
	nlohmann::json imports_add(const nlohmann::json& params) {
		const Map& m = loaded_map();
		const fs::path source = fs::absolute(required<std::string>(params, "source"));
		if (!fs::is_regular_file(source)) {
			throw Error(error_code::invalid_params, "No such file: " + source.string());
		}
		const std::string relative = params.value("path", "war3mapImported/" + source.filename().string());
		const fs::path target = map_relative(m, relative);
		if (m.imports.blacklist.contains(target.filename().string())) {
			throw Error(error_code::invalid_params, target.filename().string() + " is a map data file, the editor writes it itself");
		}
		fs::create_directories(target.parent_path());
		fs::copy_file(source, target, fs::copy_options::overwrite_existing);
		return {{"path", fs::path(relative).lexically_normal().generic_string()}, {"bytes", fs::file_size(target)}};
	}

	nlohmann::json imports_delete(const nlohmann::json& params) {
		const Map& m = loaded_map();
		const std::string relative = required<std::string>(params, "path");
		const fs::path target = map_relative(m, relative);
		if (m.imports.blacklist.contains(target.filename().string()) || !fs::is_regular_file(target)) {
			throw Error(error_code::invalid_params, "No imported file " + relative);
		}
		fs::remove(target);
		return {{"deleted", relative}};
	}

	// ---------------------------------------------------------------- Sounds (become gg_snd_<name> globals)

	nlohmann::json sound_json(const Sound& s) {
		return {
			{"name", s.name},
			{"global", "gg_snd_" + s.name},
			{"file", s.file},
			{"eax_effect", s.eax_effect},
			{"looping", s.looping},
			{"is_3d", s.is_3d},
			{"stop_out_of_range", s.stop_out_of_range},
			{"music", s.music},
			{"fade_in_rate", s.fade_in_rate},
			{"fade_out_rate", s.fade_out_rate},
			{"volume", s.volume},
			{"pitch", s.pitch},
			{"channel", s.channel},
			{"min_distance", s.min_distance},
			{"max_distance", s.max_distance},
			{"distance_cutoff", s.distance_cutoff},
		};
	}

	void apply_sound_params(Sound& s, const nlohmann::json& params) {
		if (params.contains("file")) {
			s.file = params["file"].get<std::string>();
		}
		if (params.contains("eax_effect")) {
			s.eax_effect = params["eax_effect"].get<std::string>();
		}
		for (const auto& [name, field] : {std::pair {"looping", &s.looping}, {"is_3d", &s.is_3d}, {"stop_out_of_range", &s.stop_out_of_range}, {"music", &s.music}}) {
			if (params.contains(name)) {
				*field = params[name].get<bool>();
			}
		}
		for (const auto& [name, field] : {std::pair {"fade_in_rate", &s.fade_in_rate}, {"fade_out_rate", &s.fade_out_rate}, {"volume", &s.volume}, {"channel", &s.channel}}) {
			if (params.contains(name)) {
				*field = params[name].get<int>();
			}
		}
		for (const auto& [name, field] : {std::pair {"pitch", &s.pitch}, {"min_distance", &s.min_distance}, {"max_distance", &s.max_distance}, {"distance_cutoff", &s.distance_cutoff}}) {
			if (params.contains(name)) {
				*field = params[name].get<float>();
			}
		}
	}

	Sound& find_sound(Map& m, const std::string& name) {
		for (auto& sound : m.sounds.sounds) {
			if (sound.name == name) {
				return sound;
			}
		}
		throw Error(error_code::invalid_params, "No sound " + name);
	}

	nlohmann::json sounds_list(const nlohmann::json&) {
		const Map& m = loaded_map();
		nlohmann::json result = nlohmann::json::array();
		for (const auto& sound : m.sounds.sounds) {
			result.push_back(sound_json(sound));
		}
		return result;
	}

	nlohmann::json sounds_create(const nlohmann::json& params) {
		Map& m = loaded_map();
		const std::string name = required<std::string>(params, "name");
		required<std::string>(params, "file");
		if (std::ranges::any_of(m.sounds.sounds, [&](const Sound& sound) { return sound.name == name; })) {
			throw Error(error_code::invalid_params, "A sound named " + name + " already exists");
		}
		// The World Editor's defaults for a new sound
		Sound sound {};
		sound.name = name;
		sound.eax_effect = "DefaultEAXON";
		sound.stop_out_of_range = true;
		sound.fade_in_rate = 10;
		sound.fade_out_rate = 10;
		sound.volume = -1;
		sound.pitch = 1.f;
		sound.priority = -1;
		sound.channel = 0;
		sound.min_distance = 600.f;
		sound.max_distance = 10000.f;
		sound.distance_cutoff = 3000.f;
		sound.cone_inside = 0.f;
		sound.cone_outside = 0.f;
		sound.cone_outside_volume = 127;
		apply_sound_params(sound, params);
		m.sounds.sounds.push_back(sound);
		return sound_json(sound);
	}

	nlohmann::json sounds_update(const nlohmann::json& params) {
		Map& m = loaded_map();
		Sound& sound = find_sound(m, required<std::string>(params, "name"));
		apply_sound_params(sound, params);
		if (params.contains("new_name")) {
			sound.name = params["new_name"].get<std::string>();
		}
		return sound_json(sound);
	}

	nlohmann::json sounds_delete(const nlohmann::json& params) {
		Map& m = loaded_map();
		const std::string name = required<std::string>(params, "name");
		find_sound(m, name);
		std::erase_if(m.sounds.sounds, [&](const Sound& sound) {
			return sound.name == name;
		});
		return {{"deleted", name}};
	}

	// ---------------------------------------------------------------- Export

	/// Saves the map and packs it into a .w3x archive. With "protect" the editor-only files (triggers, placed units data,
	/// regions...) are left out like HiveWE's protected export, so the archive can't be reopened for editing.
	nlohmann::json map_export(const nlohmann::json& params) {
		Map& m = loaded_map();
		if (m.is_in_temp_dir) {
			throw Error(error_code::invalid_params, "Save this new map to a folder first with map.save");
		}
		const fs::path archive = fs::absolute(required<std::string>(params, "path"));
		if (archive.extension() != ".w3x" && archive.extension() != ".w3m") {
			throw Error(error_code::invalid_params, "The archive path must end in .w3x");
		}

		host.saving_initiated();
		m.show_dialogs = false;
		const bool saved = m.save(m.filesystem_path);
		m.show_dialogs = true;
		if (!saved) {
			throw Error(error_code::internal_error, "Saving failed: " + (m.save_errors.empty() ? std::string("unknown error") : m.save_errors.front()));
		}

		const bool protect = params.value("protect", false);
		std::vector<fs::path> files;
		for (const auto& entry : fs::recursive_directory_iterator(m.filesystem_path)) {
			const fs::path relative = entry.path().lexically_relative(m.filesystem_path);
			if (entry.is_regular_file() && (!protect || !protection::is_editor_file(relative))) {
				files.push_back(relative);
			}
		}

		fs::create_directories(archive.parent_path());
		fs::remove(archive);

		SFILE_CREATE_MPQ create_info {};
		create_info.cbSize = sizeof(SFILE_CREATE_MPQ);
		create_info.dwMpqVersion = MPQ_FORMAT_VERSION_1;
		create_info.dwStreamFlags = STREAM_PROVIDER_FLAT | BASE_PROVIDER_FILE;
		create_info.dwFileFlags1 = MPQ_FILE_DEFAULT_INTERNAL;
		create_info.dwFileFlags2 = MPQ_FILE_DEFAULT_INTERNAL;
		create_info.dwAttrFlags = MPQ_ATTRIBUTE_CRC32 | MPQ_ATTRIBUTE_FILETIME | MPQ_ATTRIBUTE_MD5;
		create_info.dwSectorSize = 0x1000;
		create_info.dwMaxFileCount = static_cast<DWORD>(files.size() + 8);

		HANDLE handle;
		if (!SFileCreateArchive2(archive.wstring().c_str(), &create_info, &handle)) {
			throw Error(error_code::internal_error, std::format("Creating the archive failed (error {})", GetLastError()));
		}
		std::vector<std::string> failed;
		for (const auto& relative : files) {
			const fs::path source = m.filesystem_path / relative;
			if (!SFileAddFileEx(handle, source.wstring().c_str(), relative.string().c_str(), MPQ_FILE_COMPRESS, MPQ_COMPRESSION_ZLIB, MPQ_COMPRESSION_NEXT_SAME)) {
				failed.push_back(relative.generic_string());
			}
		}
		SFileCompactArchive(handle, nullptr, false);
		SFileCloseArchive(handle);

		return {{"path", archive.string()}, {"files", files.size() - failed.size()}, {"failed", failed}, {"warnings", m.save_errors}};
	}

	export std::vector<MethodEntry> asset_methods() {
		return {
			{"imports.list", imports_list},
			{"imports.add", imports_add},
			{"imports.delete", imports_delete},
			{"sounds.list", sounds_list},
			{"sounds.create", sounds_create},
			{"sounds.update", sounds_update},
			{"sounds.delete", sounds_delete},
			{"map.export", map_export},
		};
	}
} // namespace api
