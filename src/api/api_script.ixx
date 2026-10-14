module;

#include <lua.hpp>

export module ApiScriptMethods;

import std;
import ApiCommon;
import ApiError;
import ApiHost;
import Hierarchy;
import Map;
import MapInfo;
import TriggerStrings;
import Triggers;
import Utilities;
import <nlohmann/json.hpp>;

// The map script is JASS or Lua (mapinfo script_language). It consists of the map header (custom script code), the
// custom text triggers and HiveWE's generated code. Each enabled trigger named "My Trigger" must define a function
// InitTrig_My_Trigger, which the generated InitCustomTriggers calls. GUI triggers can be read and deleted, not edited.

namespace api {
	/// Parses Lua without running it. Returns the syntax error, or nothing.
	std::optional<std::string> lua_syntax_error(const std::string& code, const std::string& chunk_name) {
		lua_State* state = luaL_newstate();
		const int result = luaL_loadbufferx(state, code.data(), code.size(), chunk_name.c_str(), "t");
		std::optional<std::string> error;
		if (result != LUA_OK) {
			error = lua_tostring(state, -1);
		}
		lua_close(state);
		return error;
	}

	std::string script_extension(const Map& m) {
		return m.info.lua ? "war3map.lua" : "war3map.j";
	}

	std::string init_function_name(std::string name) {
		trim(name);
		std::ranges::replace(name, ' ', '_');
		return "InitTrig_" + name;
	}

	/// Warnings for code that won't work once generated, returned with every trigger edit
	nlohmann::json trigger_warnings(const Trigger& trigger) {
		nlohmann::json warnings = nlohmann::json::array();
		if (trigger.is_script && trigger.is_enabled && !trigger.custom_text.contains(init_function_name(trigger.name))) {
			warnings.push_back("The code doesn't define " + init_function_name(trigger.name) + ", which the generated script calls");
		}
		return warnings;
	}

	std::string category_name(const Map& m, const int id) {
		for (const auto& category : m.triggers.categories) {
			if (category.id == id) {
				return category.name;
			}
		}
		return "";
	}

	/// The id of the category with this name, created when it doesn't exist. Without a name the first category.
	int category_id(Map& m, const std::string& name) {
		for (const auto& category : m.triggers.categories) {
			if (category.classifier == Classifier::category && (name.empty() || category.name == name)) {
				return category.id;
			}
		}
		TriggerCategory category;
		category.classifier = Classifier::category;
		category.id = ++Trigger::next_id;
		category.name = name.empty() ? "Triggers" : name;
		category.parent_id = 0;
		m.triggers.categories.push_back(category);
		return category.id;
	}

	Trigger& find_trigger(Map& m, const nlohmann::json& params) {
		for (auto& trigger : m.triggers.triggers) {
			if ((params.contains("id") && trigger.id == params["id"].get<int>()) || (!params.contains("id") && params.contains("name") && trigger.name == params["name"].get<std::string>())) {
				return trigger;
			}
		}
		throw Error(error_code::invalid_params, "No such trigger, pass its id or name");
	}

	nlohmann::json trigger_summary(const Map& m, const Trigger& trigger) {
		return {
			{"id", trigger.id},
			{"name", trigger.name},
			{"category", category_name(m, trigger.parent_id)},
			{"kind", trigger.is_comment ? "comment" : trigger.is_script ? "script" : "gui"},
			{"enabled", trigger.is_enabled},
			{"run_on_initialization", trigger.run_on_initialization},
		};
	}

	nlohmann::json script_header_get(const nlohmann::json&) {
		const Map& m = loaded_map();
		host.close_trigger_editor();
		return {
			{"language", m.info.lua ? "lua" : "jass"},
			{"code", m.triggers.global_jass},
			{"comment", m.triggers.global_jass_comment},
		};
	}

	/// Replaces the map header code: the place for libraries, globals and a TypeScript-to-Lua bundle.
	/// Lua code is syntax checked first.
	nlohmann::json script_header_set(const nlohmann::json& params) {
		Map& m = loaded_map();
		host.close_trigger_editor();
		if (params.contains("code")) {
			const std::string code = params["code"].get<std::string>();
			if (m.info.lua) {
				if (const auto error = lua_syntax_error(code, "map header")) {
					throw Error(error_code::invalid_params, "Lua syntax error: " + *error);
				}
			}
			m.triggers.global_jass = code;
		}
		if (params.contains("comment")) {
			m.triggers.global_jass_comment = params["comment"].get<std::string>();
		}
		return {{"characters", m.triggers.global_jass.size()}};
	}

	nlohmann::json triggers_list(const nlohmann::json&) {
		Map& m = loaded_map();
		host.close_trigger_editor();
		nlohmann::json result = nlohmann::json::array();
		for (const auto& trigger : m.triggers.triggers) {
			result.push_back(trigger_summary(m, trigger));
		}
		return result;
	}

	nlohmann::json triggers_get(const nlohmann::json& params) {
		Map& m = loaded_map();
		host.close_trigger_editor();
		const Trigger& trigger = find_trigger(m, params);
		nlohmann::json result = trigger_summary(m, trigger);
		result["description"] = trigger.description;
		if (trigger.is_script) {
			result["code"] = trigger.custom_text;
		} else if (!trigger.is_comment) {
			result["note"] = "GUI trigger: its events, conditions and actions can't be read as code. Replace it with a script trigger.";
		}
		return result;
	}

	void apply_trigger_params(Map& m, Trigger& trigger, const nlohmann::json& params) {
		if (params.contains("new_name") || (params.contains("name") && params.contains("id"))) {
			const std::string name = params.contains("new_name") ? params["new_name"].get<std::string>() : params["name"].get<std::string>();
			for (const auto& other : m.triggers.triggers) {
				if (other.id != trigger.id && other.name == name) {
					throw Error(error_code::invalid_params, "A trigger named " + name + " already exists");
				}
			}
			trigger.name = name;
		}
		if (params.contains("code")) {
			if (!trigger.is_script) {
				throw Error(error_code::invalid_params, "GUI triggers can't take code, delete it and create a script trigger");
			}
			trigger.custom_text = params["code"].get<std::string>();
		}
		if (params.contains("description")) {
			trigger.description = params["description"].get<std::string>();
		}
		if (params.contains("enabled")) {
			trigger.is_enabled = params["enabled"].get<bool>();
		}
		if (params.contains("run_on_initialization")) {
			trigger.run_on_initialization = params["run_on_initialization"].get<bool>();
		}
		if (params.contains("category")) {
			trigger.parent_id = category_id(m, params["category"].get<std::string>());
		}
	}

	/// Creates a custom script trigger. "code" should define InitTrig_<name with underscores>.
	nlohmann::json triggers_create(const nlohmann::json& params) {
		Map& m = loaded_map();
		host.close_trigger_editor();
		const std::string name = required<std::string>(params, "name");
		for (const auto& other : m.triggers.triggers) {
			if (other.name == name) {
				throw Error(error_code::invalid_params, "A trigger named " + name + " already exists, use triggers.update");
			}
		}

		Trigger trigger;
		trigger.classifier = Classifier::script;
		trigger.id = ++Trigger::next_id;
		trigger.name = name;
		trigger.is_script = true;
		trigger.parent_id = category_id(m, params.value("category", ""));
		trigger.custom_text = params.value("code", "");
		apply_trigger_params(m, trigger, params);
		m.triggers.triggers.push_back(trigger);

		nlohmann::json result = trigger_summary(m, trigger);
		result["warnings"] = trigger_warnings(trigger);
		return result;
	}

	nlohmann::json triggers_update(const nlohmann::json& params) {
		Map& m = loaded_map();
		host.close_trigger_editor();
		Trigger& trigger = find_trigger(m, params);
		Trigger updated = trigger;
		apply_trigger_params(m, updated, params);
		trigger = updated;
		nlohmann::json result = trigger_summary(m, trigger);
		result["warnings"] = trigger_warnings(trigger);
		return result;
	}

	nlohmann::json triggers_delete(const nlohmann::json& params) {
		Map& m = loaded_map();
		host.close_trigger_editor();
		const int id = find_trigger(m, params).id;
		std::erase_if(m.triggers.triggers, [&](const Trigger& trigger) {
			return trigger.id == id;
		});
		return {{"deleted", id}};
	}

	// ---------------------------------------------------------------- Variables (become udg_<name> globals)

	nlohmann::json variable_json(const TriggerVariable& variable) {
		nlohmann::json result = {
			{"name", variable.name},
			{"global", "udg_" + variable.name},
			{"type", variable.type},
			{"is_array", variable.is_array},
		};
		if (variable.is_array) {
			result["array_size"] = variable.array_size;
		}
		if (variable.is_initialized) {
			result["initial_value"] = variable.initial_value;
		}
		return result;
	}

	TriggerVariable& find_variable(Map& m, const std::string& name) {
		for (auto& variable : m.triggers.variables) {
			if (variable.name == name) {
				return variable;
			}
		}
		throw Error(error_code::invalid_params, "No variable " + name);
	}

	void apply_variable_params(Map& m, TriggerVariable& variable, const nlohmann::json& params) {
		if (params.contains("type")) {
			const std::string type = params["type"].get<std::string>();
			if (m.triggers.trigger_data.data("TriggerTypes", type).empty()) {
				throw Error(error_code::invalid_params, "Unknown variable type " + type + " (examples: integer, real, boolean, string, unit, unitcode, player, location, timer, group)");
			}
			variable.type = type;
		}
		if (params.contains("is_array")) {
			variable.is_array = params["is_array"].get<bool>();
		}
		if (params.contains("array_size")) {
			variable.array_size = params["array_size"].get<int>();
		}
		if (params.contains("initial_value")) {
			const nlohmann::json& value = params["initial_value"];
			variable.is_initialized = !value.is_null();
			variable.initial_value = value.is_null() ? "" : value.is_string() ? value.get<std::string>() : value.dump();
		}
	}

	nlohmann::json variables_list(const nlohmann::json&) {
		Map& m = loaded_map();
		host.close_trigger_editor();
		nlohmann::json result = nlohmann::json::array();
		for (const auto& variable : m.triggers.variables) {
			result.push_back(variable_json(variable));
		}
		return result;
	}

	nlohmann::json variables_create(const nlohmann::json& params) {
		Map& m = loaded_map();
		host.close_trigger_editor();
		const std::string name = required<std::string>(params, "name");
		required<std::string>(params, "type");
		if (std::ranges::any_of(m.triggers.variables, [&](const TriggerVariable& variable) { return variable.name == name; })) {
			throw Error(error_code::invalid_params, "A variable named " + name + " already exists");
		}
		TriggerVariable variable;
		variable.id = ++Trigger::next_id;
		variable.name = name;
		variable.unknown = 1;
		variable.is_array = false;
		variable.is_initialized = false;
		variable.parent_id = category_id(m, params.value("category", ""));
		apply_variable_params(m, variable, params);
		m.triggers.variables.push_back(variable);
		return variable_json(variable);
	}

	nlohmann::json variables_update(const nlohmann::json& params) {
		Map& m = loaded_map();
		host.close_trigger_editor();
		TriggerVariable& variable = find_variable(m, required<std::string>(params, "name"));
		TriggerVariable updated = variable;
		apply_variable_params(m, updated, params);
		if (params.contains("new_name")) {
			updated.name = params["new_name"].get<std::string>();
		}
		variable = updated;
		return variable_json(variable);
	}

	nlohmann::json variables_delete(const nlohmann::json& params) {
		Map& m = loaded_map();
		host.close_trigger_editor();
		const std::string name = required<std::string>(params, "name");
		find_variable(m, name);
		std::erase_if(m.triggers.variables, [&](const TriggerVariable& variable) {
			return variable.name == name;
		});
		return {{"deleted", name}};
	}

	// ---------------------------------------------------------------- Trigger strings (war3map.wts)

	nlohmann::json strings_list(const nlohmann::json& params) {
		const Map& m = loaded_map();
		const std::string search = to_lowercase_copy(params.value("search", ""));
		nlohmann::json result = nlohmann::json::object();
		for (const auto& [key, value] : m.trigger_strings.all()) {
			if (search.empty() || to_lowercase_copy(value).contains(search)) {
				result[key] = value;
			}
		}
		return result;
	}

	/// Sets the text of a TRIGSTR_ key, or creates a new key without "key". Use the key in object fields or scripts.
	nlohmann::json strings_set(const nlohmann::json& params) {
		Map& m = loaded_map();
		std::string key = params.value("key", "");
		if (!key.empty() && !key.starts_with("TRIGSTR_")) {
			throw Error(error_code::invalid_params, "Keys look like TRIGSTR_001");
		}
		const std::string value = required<std::string>(params, "value");
		if (key.empty() && value.empty()) {
			throw Error(error_code::invalid_params, "A new string needs a value");
		}
		m.trigger_strings.set_string(key, value);
		return {{"key", key}, {"value", value}};
	}

	// ---------------------------------------------------------------- Building

	/// Generates and compiles the map script like saving does (JASS goes through JassHelper), without saving the map
	nlohmann::json script_build(const nlohmann::json&) {
		Map& m = loaded_map();
		host.close_trigger_editor();
		const auto result = m.triggers.generate_map_script(
			m.terrain,
			m.units,
			m.doodads,
			m.info,
			m.sounds,
			m.regions,
			m.cameras,
			m.tilesets,
			m.info.lua ? ScriptMode::lua : ScriptMode::jass
		);
		if (!result) {
			return {{"ok", false}, {"errors", result.error()}};
		}
		const std::filesystem::path file = m.filesystem_path / script_extension(m);
		// HiveWE only writes Lua out, so check its syntax here; the line numbers refer to the generated war3map.lua
		if (m.info.lua) {
			std::ifstream stream(file, std::ios::binary);
			const std::string code((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			if (const auto error = lua_syntax_error(code, "war3map.lua")) {
				return {{"ok", false}, {"errors", *error}, {"file", file.string()}};
			}
		}
		return {{"ok", true}, {"file", file.string()}};
	}

	/// The generated war3map.j or war3map.lua from the last save or script.build, optionally a line range
	nlohmann::json script_generated(const nlohmann::json& params) {
		const Map& m = loaded_map();
		const std::filesystem::path path = m.filesystem_path / script_extension(m);
		std::ifstream file(path, std::ios::binary);
		if (!file) {
			throw Error(error_code::invalid_params, "No generated script yet, call script.build first");
		}
		std::vector<std::string> lines;
		for (std::string line; std::getline(file, line);) {
			if (line.ends_with('\r')) {
				line.pop_back();
			}
			lines.push_back(std::move(line));
		}
		const size_t start = std::min(static_cast<size_t>(std::max(params.value("from_line", 1), 1) - 1), lines.size());
		const size_t count = params.value("line_count", 400);
		std::string text;
		for (size_t i = start; i < lines.size() && i < start + count; i++) {
			text += std::format("{}: {}\n", i + 1, lines[i]);
		}
		return {{"total_lines", lines.size()}, {"text", text}};
	}

	export std::vector<MethodEntry> script_methods() {
		return {
			{"script.header.get", script_header_get},
			{"script.header.set", script_header_set},
			{"script.build", script_build},
			{"script.generated", script_generated},
			{"triggers.list", triggers_list},
			{"triggers.get", triggers_get},
			{"triggers.create", triggers_create},
			{"triggers.update", triggers_update},
			{"triggers.delete", triggers_delete},
			{"variables.list", variables_list},
			{"variables.create", variables_create},
			{"variables.update", variables_update},
			{"variables.delete", variables_delete},
			{"strings.list", strings_list},
			{"strings.set", strings_set},
		};
	}
} // namespace api
