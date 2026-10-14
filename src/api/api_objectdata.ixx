module;

#include <QAbstractTableModel>

export module ApiObjectDataMethods;

import std;
import ApiCommon;
import ApiError;
import GameplayConstants;
import Globals;
import Map;
import MapGlobal;
import SLK;
import TableModel;
import TriggerStrings;
import Utilities;
import <nlohmann/json.hpp>;

// Object data is edited through the object editor's table models, so open editors and placed objects update live.
// Fields are named like the SLK columns: lowercase, e.g. "hp", "name", "abilities". Leveled fields append the level,
// e.g. "dataa1" or "cool3", and fields with an extra data letter put it before the level ("dataa", "datab").

namespace api {
	TableModel& table_for(const nlohmann::json& params) {
		static const std::unordered_map<std::string, TableModel** > tables = {
			{"unit", &units_table},
			{"item", &items_table},
			{"ability", &abilities_table},
			{"doodad", &doodads_table},
			{"destructible", &destructibles_table},
			{"upgrade", &upgrade_table},
			{"buff", &buff_table},
		};
		loaded_map();
		const auto found = tables.find(required<std::string>(params, "kind"));
		if (found == tables.end() || *found->second == nullptr) {
			throw Error(error_code::invalid_params, "kind must be unit, item, ability, doodad, destructible, upgrade or buff");
		}
		return **found->second;
	}

	void require_object(const TableModel& table, const std::string& id) {
		if (!table.slk->row_headers.contains(id)) {
			throw Error(error_code::invalid_params, "No object with id " + id);
		}
	}

	bool is_custom(const slk::SLK& slk, const std::string& id) {
		return slk.shadow_data.contains(id) && slk.shadow_data.at(id).contains("oldid");
	}

	/// SLK values are strings, the API also takes numbers, booleans and lists
	std::string to_slk_value(const nlohmann::json& value) {
		if (value.is_string()) {
			return value.get<std::string>();
		}
		if (value.is_boolean()) {
			return value.get<bool>() ? "1" : "0";
		}
		if (value.is_number_integer()) {
			return std::to_string(value.get<long long>());
		}
		if (value.is_number()) {
			return std::format("{}", value.get<double>());
		}
		if (value.is_array()) {
			std::string joined;
			for (const auto& item : value) {
				if (!joined.empty()) {
					joined += ',';
				}
				joined += to_slk_value(item);
			}
			return joined;
		}
		throw Error(error_code::invalid_params, "Field values must be strings, numbers, booleans or lists");
	}

	std::string display_string(const std::string& value) {
		if (map && value.starts_with("TRIGSTR_")) {
			return std::string(map->trigger_strings.string(value));
		}
		return value;
	}

	struct FieldInfo {
		std::string meta_key;
		std::string column;
		int level;
	};

	/// The fields an object shows in the object editor, like SingleModel::buildMapping
	std::vector<FieldInfo> object_fields(const slk::SLK& slk, const slk::SLK& meta_slk, const std::string& id) {
		std::string_view base_id = id;
		if (is_custom(slk, id)) {
			base_id = slk.shadow_data.at(id).at("oldid");
		}
		const std::string_view alias = slk.column_headers.contains("code") ? slk.data<std::string_view>("code", id) : std::string_view();

		std::vector<FieldInfo> fields;
		for (const auto& [key, index] : meta_slk.row_headers) {
			if (meta_slk.column_headers.contains("usespecific")) {
				const std::string_view use_specific = meta_slk.data<std::string_view>("usespecific", key);
				if (!use_specific.empty() && !use_specific.contains(base_id) && (alias.empty() || !use_specific.contains(alias))) {
					continue;
				}
			}
			if (meta_slk.column_headers.contains("notspecific")) {
				const std::string_view not_specific = meta_slk.data<std::string_view>("notspecific", key);
				if (!not_specific.empty() && not_specific.contains(base_id) && (alias.empty() || not_specific.contains(alias))) {
					continue;
				}
			}

			std::string field = to_lowercase_copy(meta_slk.data<std::string_view>("field", key));
			if (meta_slk.column_headers.contains("data") && meta_slk.data<int>("data", key) > 0) {
				field += static_cast<char>('a' + (meta_slk.data<int>("data", key) - 1));
			}

			if (meta_slk.column_headers.contains("repeat") && meta_slk.data<int>("repeat", key) > 0) {
				int levels = 1;
				if (slk.column_headers.contains("levels")) {
					levels = slk.data<int>("levels", id);
				} else if (slk.column_headers.contains("numvar")) {
					levels = slk.data<int>("numvar", id);
				} else if (slk.column_headers.contains("maxlevel")) {
					levels = slk.data<int>("maxlevel", id);
				}
				const bool append_index = meta_slk.column_headers.contains("appendindex") && meta_slk.data<int>("appendindex", key) > 0;
				for (int i = 0; i < std::max(levels, 1); i++) {
					std::string column = append_index ? (i == 0 ? field : field + std::to_string(i)) : field + std::to_string(i + 1);
					fields.push_back({key, std::move(column), i + 1});
				}
			} else {
				fields.push_back({key, field, 0});
			}
		}
		std::ranges::sort(fields, {}, &FieldInfo::column);
		return fields;
	}

	/// Sets one field through the table model, so views and placed objects refresh
	void set_field(TableModel& table, const std::string& id, const std::string& column, const std::string& value) {
		if (column == "oldid") {
			throw Error(error_code::invalid_params, "oldid can't be changed");
		}
		// Saving writes every field through the meta SLK, an unknown field would make the save fail
		if (!table.slk->field_to_meta_id(*table.meta_slk, column, id)) {
			throw Error(error_code::invalid_params, std::format("{} has no field {}, see objects.fields", id, column));
		}
		table.slk->set_shadow_data(column, id, value);
		const QModelIndex index = table.index(static_cast<int>(table.slk->row_headers.at(id)), static_cast<int>(table.slk->column_headers.at(column)));
		emit table.dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
	}

	nlohmann::json object_summary(const TableModel& table, const std::string& id) {
		const slk::SLK& slk = *table.slk;
		nlohmann::json summary = {
			{"id", id},
			{"name", display_string(slk.data("name", id))},
			{"custom", is_custom(slk, id)},
		};
		if (slk.column_headers.contains("editorsuffix")) {
			const std::string suffix = display_string(slk.data("editorsuffix", id));
			if (!suffix.empty()) {
				summary["suffix"] = suffix;
			}
		}
		if (is_custom(slk, id)) {
			summary["base"] = slk.shadow_data.at(id).at("oldid");
		}
		return summary;
	}

	nlohmann::json objects_list(const nlohmann::json& params) {
		const TableModel& table = table_for(params);
		const slk::SLK& slk = *table.slk;
		const bool custom_only = params.value("custom_only", false);
		const std::string search = to_lowercase_copy(params.value("search", ""));
		const size_t limit = params.value("limit", 200);

		std::vector<std::string> ids;
		for (const auto& [id, index] : slk.row_headers) {
			if (custom_only && !is_custom(slk, id)) {
				continue;
			}
			if (!search.empty()) {
				const std::string name = to_lowercase_copy(display_string(slk.data("name", id)));
				if (!name.contains(search) && !to_lowercase_copy(id).contains(search)) {
					continue;
				}
			}
			ids.push_back(id);
		}
		std::ranges::sort(ids);

		nlohmann::json objects = nlohmann::json::array();
		for (size_t i = 0; i < ids.size() && i < limit; i++) {
			objects.push_back(object_summary(table, ids[i]));
		}
		return {{"total", ids.size()}, {"objects", objects}};
	}

	/// Searches every kind at once by name or id, for when the kind isn't known (trees are destructibles, for example)
	nlohmann::json objects_search(const nlohmann::json& params) {
		const std::string search = required<std::string>(params, "search");
		const size_t limit = params.value("limit", 60);
		nlohmann::json results = nlohmann::json::array();
		for (const char* kind : {"unit", "item", "ability", "doodad", "destructible", "upgrade", "buff"}) {
			nlohmann::json list = objects_list({{"kind", kind}, {"search", search}, {"limit", limit}});
			for (auto& object : list["objects"]) {
				object["kind"] = kind;
				results.push_back(std::move(object));
			}
		}
		if (results.size() > limit) {
			results.erase(results.begin() + static_cast<std::ptrdiff_t>(limit), results.end());
		}
		return results;
	}

	/// An object's modified fields, or with "all" every field value, or just the requested "fields"
	nlohmann::json objects_get(const nlohmann::json& params) {
		const TableModel& table = table_for(params);
		const slk::SLK& slk = *table.slk;
		const std::string id = required<std::string>(params, "id");
		require_object(table, id);

		nlohmann::json result = object_summary(table, id);
		nlohmann::json modified = nlohmann::json::object();
		if (slk.shadow_data.contains(id)) {
			for (const auto& [column, value] : slk.shadow_data.at(id)) {
				if (column != "oldid") {
					modified[column] = value;
				}
			}
		}
		result["modified"] = modified;

		if (params.contains("fields")) {
			nlohmann::json values = nlohmann::json::object();
			for (const auto& column : params["fields"].get<std::vector<std::string>>()) {
				values[column] = slk.data(to_lowercase_copy(column), id);
			}
			result["fields"] = values;
		} else if (params.value("all", false)) {
			nlohmann::json values = nlohmann::json::object();
			for (const auto& field : object_fields(slk, *table.meta_slk, id)) {
				values[field.column] = slk.data(field.column, id);
			}
			result["fields"] = values;
		}
		return result;
	}

	/// The editable fields of an object, with their display names, types and categories
	nlohmann::json objects_fields(const nlohmann::json& params) {
		const TableModel& table = table_for(params);
		const slk::SLK& meta = *table.meta_slk;
		const std::string id = required<std::string>(params, "id");
		require_object(table, id);
		const std::string search = to_lowercase_copy(params.value("search", ""));

		nlohmann::json fields = nlohmann::json::array();
		for (const auto& field : object_fields(*table.slk, meta, id)) {
			const std::string name = meta.data("displayname", field.meta_key);
			if (!search.empty() && !to_lowercase_copy(name).contains(search) && !field.column.contains(search)) {
				continue;
			}
			nlohmann::json entry = {
				{"field", field.column},
				{"name", name},
				{"type", meta.data("type", field.meta_key)},
				{"category", meta.column_headers.contains("category") ? meta.data("category", field.meta_key) : ""},
			};
			if (field.level > 0) {
				entry["level"] = field.level;
			}
			for (const char* limit : {"minval", "maxval"}) {
				if (meta.column_headers.contains(limit)) {
					const std::string value = meta.data(limit, field.meta_key);
					if (!value.empty() && value != "-") {
						entry[limit] = value;
					}
				}
			}
			fields.push_back(std::move(entry));
		}
		return fields;
	}

	/// Copies "base" into a new custom object. "id" defaults to a free one, "fields" are set right away.
	nlohmann::json objects_create(const nlohmann::json& params) {
		TableModel& table = table_for(params);
		const std::string base = required<std::string>(params, "base");
		require_object(table, base);
		if (is_custom(*table.slk, base)) {
			throw Error(error_code::invalid_params, base + " is a custom object, base new objects on a standard one");
		}

		// Heroes and other uppercase IDs keep an uppercase first letter, the game relies on it
		std::string id = params.contains("id") ? params["id"].get<std::string>() : get_unique_id(std::isupper(static_cast<unsigned char>(base.front())));
		if (id.size() != 4) {
			throw Error(error_code::invalid_params, "Object ids are 4 characters");
		}
		if (table.slk->row_headers.contains(id)) {
			throw Error(error_code::invalid_params, "The id " + id + " is already used");
		}

		// A copy has its base's fields, so validate against the base before creating anything
		const nlohmann::json fields = params.value("fields", nlohmann::json::object());
		for (const auto& [column, value] : fields.items()) {
			if (!table.slk->field_to_meta_id(*table.meta_slk, to_lowercase_copy(column), base)) {
				throw Error(error_code::invalid_params, std::format("{} has no field {}, see objects.fields", base, column));
			}
			to_slk_value(value);
		}

		table.copyRow(base, id);
		if (params.contains("name")) {
			set_field(table, id, "name", params["name"].get<std::string>());
		}
		for (const auto& [column, value] : fields.items()) {
			set_field(table, id, to_lowercase_copy(column), to_slk_value(value));
		}
		return object_summary(table, id);
	}

	nlohmann::json objects_set(const nlohmann::json& params) {
		TableModel& table = table_for(params);
		const std::string id = required<std::string>(params, "id");
		require_object(table, id);
		const nlohmann::json fields = required<nlohmann::json>(params, "fields");

		// Validate everything first so a bad field changes nothing
		for (const auto& [column, value] : fields.items()) {
			if (!table.slk->field_to_meta_id(*table.meta_slk, to_lowercase_copy(column), id)) {
				throw Error(error_code::invalid_params, std::format("{} has no field {}, see objects.fields", id, column));
			}
			to_slk_value(value);
		}
		for (const auto& [column, value] : fields.items()) {
			set_field(table, id, to_lowercase_copy(column), to_slk_value(value));
		}
		return objects_get({{"kind", params["kind"]}, {"id", id}});
	}

	/// Restores fields to their defaults (the base object's values for custom objects), all modified ones without "fields"
	nlohmann::json objects_reset(const nlohmann::json& params) {
		TableModel& table = table_for(params);
		const std::string id = required<std::string>(params, "id");
		require_object(table, id);
		slk::SLK& slk = *table.slk;

		std::vector<std::string> columns;
		if (params.contains("fields")) {
			columns = params["fields"].get<std::vector<std::string>>();
		} else if (slk.shadow_data.contains(id)) {
			for (const auto& [column, value] : slk.shadow_data.at(id)) {
				if (column != "oldid") {
					columns.push_back(column);
				}
			}
		}
		for (auto column : columns) {
			column = to_lowercase_copy(column);
			if (column == "oldid" || !slk.shadow_data.contains(id) || !slk.shadow_data.at(id).contains(column)) {
				continue;
			}
			slk.shadow_data.at(id).erase(column);
			if (slk.column_headers.contains(column)) {
				const QModelIndex index = table.index(static_cast<int>(slk.row_headers.at(id)), static_cast<int>(slk.column_headers.at(column)));
				emit table.dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
			}
		}
		return objects_get({{"kind", params["kind"]}, {"id", id}});
	}

	/// Deletes a custom object. Placed units of it are removed too, like in the object editor.
	nlohmann::json objects_delete(const nlohmann::json& params) {
		TableModel& table = table_for(params);
		const std::string id = required<std::string>(params, "id");
		require_object(table, id);
		if (!is_custom(*table.slk, id)) {
			throw Error(error_code::invalid_params, "Only custom objects can be deleted");
		}
		table.deleteRow(id);
		return {{"deleted", id}};
	}

	nlohmann::json constants_get(const nlohmann::json& params) {
		const Map& m = loaded_map();
		const slk::SLK& data = m.gameplay_constants.data;
		nlohmann::json result = nlohmann::json::object();
		if (params.contains("fields")) {
			for (const auto& column : params["fields"].get<std::vector<std::string>>()) {
				result[column] = data.data(to_lowercase_copy(column), constants_row_key);
			}
			return result;
		}
		if (params.value("all", false)) {
			for (const auto& [column, index] : data.column_headers) {
				result[column] = data.data(column, constants_row_key);
			}
			return result;
		}
		if (data.shadow_data.contains(constants_row_key)) {
			for (const auto& [column, value] : data.shadow_data.at(constants_row_key)) {
				result[column] = value;
			}
		}
		return {{"modified", result}};
	}

	nlohmann::json constants_set(const nlohmann::json& params) {
		Map& m = loaded_map();
		slk::SLK& data = m.gameplay_constants.data;
		const nlohmann::json fields = required<nlohmann::json>(params, "fields");
		for (const auto& [column, value] : fields.items()) {
			if (!data.column_headers.contains(to_lowercase_copy(column))) {
				throw Error(error_code::invalid_params, "Unknown gameplay constant " + column + ", see constants.get with all=true");
			}
		}
		for (const auto& [column, value] : fields.items()) {
			data.set_shadow_data(to_lowercase_copy(column), constants_row_key, to_slk_value(value));
		}
		return constants_get(nlohmann::json::object());
	}

	nlohmann::json constants_reset(const nlohmann::json& params) {
		Map& m = loaded_map();
		slk::SLK& data = m.gameplay_constants.data;
		if (!data.shadow_data.contains(constants_row_key)) {
			return constants_get(nlohmann::json::object());
		}
		if (params.contains("fields")) {
			for (const auto& column : params["fields"].get<std::vector<std::string>>()) {
				data.shadow_data.at(constants_row_key).erase(to_lowercase_copy(column));
			}
		} else {
			data.shadow_data.erase(constants_row_key);
		}
		return constants_get(nlohmann::json::object());
	}

	export std::vector<MethodEntry> object_data_methods() {
		return {
			{"objects.list", objects_list},
			{"objects.search", objects_search},
			{"objects.get", objects_get},
			{"objects.fields", objects_fields},
			{"objects.create", objects_create},
			{"objects.set", objects_set},
			{"objects.reset", objects_reset},
			{"objects.delete", objects_delete},
			{"constants.get", constants_get},
			{"constants.set", constants_set},
			{"constants.reset", constants_reset},
		};
	}
} // namespace api
