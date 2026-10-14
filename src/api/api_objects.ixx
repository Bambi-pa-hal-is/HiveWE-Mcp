module;

#include <QRectF>

export module ApiObjectMethods;

import std;
import ApiCommon;
import ApiError;
import Doodad;
import Doodads;
import GameCameras;
import Globals;
import Hierarchy;
import Map;
import PathingMap;
import Rects;
import Regions;
import Skeleton;
import SkinnedMesh;
import SLK;
import Terrain;
import Units;
import Utilities;
import WorldUndoManager;
import <glm/glm.hpp>;
import <nlohmann/json.hpp>;

// Objects are identified by their creation number ("id"). "type" is the four letter object ID such as hfoo or LTlt.

namespace api {
	/// One API edit of doodads: the ones removed, added and changed. redo() applies it, so the API applies edits through it too.
	class DoodadEdit final : public WorldCommand {
	  public:
		std::vector<Doodad> removed;
		std::vector<Doodad> added;
		std::vector<std::pair<Doodad, Doodad>> changed;

		void undo(WorldEditContext& ctx) override {
			apply(ctx, added, removed, false);
		}

		void redo(WorldEditContext& ctx) override {
			apply(ctx, removed, added, true);
		}

	  private:
		void apply(WorldEditContext& ctx, const std::vector<Doodad>& to_remove, const std::vector<Doodad>& to_add, const bool forward) const {
			PathingRect area;
			std::unordered_set<int> remove_numbers;
			for (const auto& i : to_remove) {
				remove_numbers.insert(i.creation_number);
				area |= i.get_pathing_bounding_box();
			}
			std::erase_if(ctx.doodads.doodads, [&](const Doodad& doodad) {
				return remove_numbers.contains(doodad.creation_number);
			});
			for (const auto& i : to_add) {
				ctx.doodads.doodads.push_back(i);
				area |= i.get_pathing_bounding_box();
			}
			for (const auto& [before, after] : changed) {
				const Doodad& target = forward ? after : before;
				for (auto& j : ctx.doodads.doodads) {
					if (j.creation_number == target.creation_number) {
						area |= j.get_pathing_bounding_box();
						j = target;
						area |= j.get_pathing_bounding_box();
					}
				}
			}
			ctx.doodads.update_doodad_pathing(area, ctx.pathing_map);
		}
	};

	/// The same for units or placed items
	class UnitEdit final : public WorldCommand {
	  public:
		bool items = false;
		std::vector<Unit> removed;
		std::vector<Unit> added;
		std::vector<std::pair<Unit, Unit>> changed;

		void undo(WorldEditContext& ctx) override {
			apply(ctx, added, removed, false);
		}

		void redo(WorldEditContext& ctx) override {
			apply(ctx, removed, added, true);
		}

	  private:
		void apply(WorldEditContext& ctx, const std::vector<Unit>& to_remove, const std::vector<Unit>& to_add, const bool forward) const {
			std::vector<Unit>& list = items ? ctx.units.items : ctx.units.units;
			std::unordered_set<int> remove_numbers;
			for (const auto& i : to_remove) {
				remove_numbers.insert(i.creation_number);
			}
			std::erase_if(list, [&](const Unit& unit) {
				return remove_numbers.contains(unit.creation_number);
			});
			list.insert(list.end(), to_add.begin(), to_add.end());
			for (const auto& [before, after] : changed) {
				const Unit& target = forward ? after : before;
				for (auto& j : list) {
					if (j.creation_number == target.creation_number) {
						j = target;
					}
				}
			}
		}
	};

	/// Regions are few and small, so their edits keep the whole list
	class RegionsEdit final : public WorldCommand {
	  public:
		std::vector<Region> before;
		std::vector<Region> after;

		void undo(WorldEditContext& ctx) override {
			ctx.regions.regions = before;
		}

		void redo(WorldEditContext& ctx) override {
			ctx.regions.regions = after;
		}
	};

	/// Applies the edit and makes it one undo step
	void commit(Map& m, std::unique_ptr<WorldCommand> edit) {
		auto context = m.edit_context();
		edit->redo(context);
		m.world_undo.new_undo_group();
		m.world_undo.add_undo_action(std::move(edit));
	}

	std::vector<int> id_list(const nlohmann::json& params) {
		if (params.contains("ids")) {
			return params["ids"].get<std::vector<int>>();
		}
		return {required<int>(params, "id")};
	}

	/// The x/y/radius or rect area filter of list methods, in internal coordinates
	std::optional<QRectF> area_filter(const Map& m, const nlohmann::json& params) {
		if (params.contains("radius")) {
			const glm::vec2 center = world_to_internal(m, glm::vec2(required<float>(params, "x"), required<float>(params, "y")));
			const float radius = params["radius"].get<float>() / 128.f;
			return QRectF(center.x - radius, center.y - radius, radius * 2, radius * 2);
		}
		if (params.contains("left")) {
			const glm::vec2 min = world_to_internal(m, glm::vec2(required<float>(params, "left"), required<float>(params, "bottom")));
			const glm::vec2 max = world_to_internal(m, glm::vec2(required<float>(params, "right"), required<float>(params, "top")));
			return QRectF(min.x, min.y, max.x - min.x, max.y - min.y);
		}
		return std::nullopt;
	}

	nlohmann::json item_sets_json(const std::vector<ItemSet>& sets) {
		nlohmann::json result = nlohmann::json::array();
		for (const auto& set : sets) {
			nlohmann::json items = nlohmann::json::array();
			for (const auto& [chance, id] : set.items) {
				items.push_back({{"type", id}, {"chance", chance}});
			}
			result.push_back(items);
		}
		return result;
	}

	std::vector<ItemSet> parse_item_sets(const nlohmann::json& value) {
		std::vector<ItemSet> sets;
		for (const auto& set : value) {
			ItemSet item_set;
			for (const auto& item : set) {
				item_set.items.emplace_back(item.value("chance", 100), required<std::string>(item, "type"));
			}
			sets.push_back(item_set);
		}
		return sets;
	}

	// ---------------------------------------------------------------- Doodads and destructibles

	constexpr std::array doodad_states = {"invisible_non_solid", "visible_non_solid", "visible_solid"};

	nlohmann::json doodad_json(const Map& m, const Doodad& d) {
		const glm::vec2 world = internal_to_world(m, glm::vec2(d.position));
		return {
			{"id", d.creation_number},
			{"type", d.id},
			{"skin", d.skin_id},
			{"destructible", !doodads_slk.row_headers.contains(d.id)},
			{"variation", d.variation},
			{"x", world.x},
			{"y", world.y},
			{"z", d.position.z * 128.f},
			{"facing", glm::degrees(d.angle)},
			{"scale", {d.scale.x, d.scale.y, d.scale.z}},
			{"state", doodad_states[static_cast<int>(d.state)]},
			{"life", d.life},
			{"item_drops", item_sets_json(d.item_sets)},
		};
	}

	Doodad& find_doodad(Map& m, const int id) {
		for (auto& i : m.doodads.doodads) {
			if (i.creation_number == id) {
				return i;
			}
		}
		throw Error(error_code::invalid_params, std::format("No doodad with id {}", id));
	}

	/// Applies the editable doodad fields in params. Positions snap to the pathing grid like the doodad palette.
	void apply_doodad_params(const Map& m, Doodad& d, const nlohmann::json& params) {
		const bool is_doodad = doodads_slk.row_headers.contains(d.id);
		if (params.contains("variation")) {
			const int variations = std::max(1, (is_doodad ? doodads_slk : destructibles_slk).data<int>("numvar", d.id));
			d.variation = std::clamp(params["variation"].get<int>(), 0, variations - 1);
		}
		if (params.contains("facing")) {
			// Doodads with a fixed rotation or a non square footprint only take some angles
			d.angle = Doodad::acceptable_angle(d.id, d.pathing, d.angle, glm::radians(params["facing"].get<float>()));
		}
		if (params.contains("scale")) {
			d.scale = params["scale"].is_array() ? glm::vec3(params["scale"][0], params["scale"][1], params["scale"][2]) : glm::vec3(params["scale"].get<float>());
		}
		if (params.contains("state")) {
			const auto found = std::ranges::find(doodad_states, params["state"].get<std::string>());
			if (found == doodad_states.end()) {
				throw Error(error_code::invalid_params, "state must be invisible_non_solid, visible_non_solid or visible_solid");
			}
			d.state = static_cast<Doodad::State>(found - doodad_states.begin());
		}
		if (params.contains("life")) {
			d.life = std::clamp(params["life"].get<int>(), 1, 100);
		}
		if (params.contains("skin")) {
			d.skin_id = params["skin"].get<std::string>();
		}
		if (params.contains("item_drops")) {
			d.item_sets = parse_item_sets(params["item_drops"]);
			d.item_table_pointer = -1;
		}
		if (params.contains("x") || params.contains("y")) {
			const glm::vec2 current = internal_to_world(m, glm::vec2(d.position));
			const glm::vec2 internal = world_to_internal(m, glm::vec2(params.value("x", current.x), params.value("y", current.y)));
			const glm::vec2 snapped = Doodad::acceptable_position(internal, d.pathing, d.angle);
			d.position = glm::vec3(snapped, m.terrain.interpolated_height(snapped.x, snapped.y, true));
		}
		if (params.contains("z")) {
			d.position.z = params["z"].get<float>() / 128.f;
		}
	}

	nlohmann::json doodads_list(const nlohmann::json& params) {
		const Map& m = loaded_map();
		const auto area = area_filter(m, params);
		const std::string type = params.value("type", "");
		const size_t limit = params.value("limit", 200);

		nlohmann::json result = nlohmann::json::array();
		size_t total = 0;
		for (const auto& d : m.doodads.doodads) {
			if ((!type.empty() && d.id != type) || (area && !area->contains(d.position.x, d.position.y))) {
				continue;
			}
			if (total++ < limit) {
				result.push_back(doodad_json(m, d));
			}
		}
		return {{"total", total}, {"doodads", result}};
	}

	nlohmann::json doodads_get(const nlohmann::json& params) {
		Map& m = loaded_map();
		return doodad_json(m, find_doodad(m, required<int>(params, "id")));
	}

	/// Creates one doodad, or many with "doodads": [{type, x, y, ...}, ...] as one undo step
	/// The variations of a doodad or destructible type that have a model in the current graphics mode, following the
	/// file naming Doodads::get_mesh uses
	std::vector<int> model_variations(const std::string& type) {
		const bool is_doodad = doodads_slk.row_headers.contains(type);
		const slk::SLK& slk = is_doodad ? doodads_slk : destructibles_slk;
		const std::filesystem::path base = string_replaced(slk.data<std::string_view>("file", type), "\\", "/");
		const int count = std::max(1, slk.data<int>("numvar", type));
		std::vector<int> found;
		for (int i = 0; i < count; i++) {
			std::filesystem::path path = base;
			if (count > 1) {
				path.replace_filename(is_doodad ? base.filename().string() + std::to_string(i) : base.stem().string() + std::to_string(i));
			}
			if (hierarchy.file_exists(path, Hierarchy::FileSource::all, {".mdl", ".mdx"})) {
				found.push_back(i);
			}
		}
		return found;
	}

	nlohmann::json doodads_models(const nlohmann::json& params) {
		loaded_map();
		const std::string type = required<std::string>(params, "type");
		const bool is_doodad = doodads_slk.row_headers.contains(type);
		if (!is_doodad && !destructibles_slk.row_headers.contains(type)) {
			throw Error(error_code::invalid_params, "Unknown doodad or destructible type " + type);
		}
		const std::vector<int> found = model_variations(type);
		return {
			{"type", type},
			{"variations", std::max(1, (is_doodad ? doodads_slk : destructibles_slk).data<int>("numvar", type))},
			{"with_model", found},
			{"usable", !found.empty()},
		};
	}

	/// Builds a doodad from create parameters. Without "variation" a random one that has a model is picked.
	Doodad make_doodad(Map& m, const nlohmann::json& entry) {
		const std::string type = required<std::string>(entry, "type");
		const bool is_doodad = doodads_slk.row_headers.contains(type);
		if (!is_doodad && !destructibles_slk.row_headers.contains(type)) {
			throw Error(error_code::invalid_params, "Unknown doodad or destructible type " + type);
		}
		required<float>(entry, "x");
		required<float>(entry, "y");

		static std::unordered_map<std::string, std::vector<int>> usable_variations;
		if (!usable_variations.contains(type)) {
			usable_variations[type] = model_variations(type);
		}
		const std::vector<int>& usable = usable_variations[type];
		const int variations = std::max(1, (is_doodad ? doodads_slk : destructibles_slk).data<int>("numvar", type));

		Doodad d;
		if (entry.contains("variation")) {
			d.variation = std::clamp(entry["variation"].get<int>(), 0, variations - 1);
		} else {
			d.variation = usable.empty() ? 0 : usable[std::rand() % usable.size()];
		}
		d.init(type, m.doodads.get_mesh(type, d.variation), m.terrain);
		d.creation_number = ++Doodad::auto_increment;
		if (!entry.contains("facing")) {
			d.angle = Doodad::acceptable_angle(type, d.pathing, 0.f, glm::radians(270.f));
		}
		apply_doodad_params(m, d, entry);
		d.update(m.terrain);
		return d;
	}

	nlohmann::json doodads_create(const nlohmann::json& params) {
		Map& m = loaded_map();
		const nlohmann::json list = params.contains("doodads") ? params["doodads"] : nlohmann::json::array({params});

		auto edit = std::make_unique<DoodadEdit>();
		for (const auto& entry : list) {
			edit->added.push_back(make_doodad(m, entry));
		}

		nlohmann::json created = nlohmann::json::array();
		for (const auto& d : edit->added) {
			created.push_back(doodad_json(m, d));
		}
		commit(m, std::move(edit));
		return list.size() == 1 && !params.contains("doodads") ? created[0] : nlohmann::json {{"doodads", created}};
	}

	/// Places random doodads in an area: a circle (x, y, radius) or rectangle (left, bottom, right, top).
	/// count or density (per 1000x1000 units) sets how many. Points on water or unwalkable ground, closer than min_distance to
	/// another doodad, or inside an "avoid" zone ({x, y, radius} or {points, width}) are skipped. One undo step.
	nlohmann::json doodads_scatter(const nlohmann::json& params) {
		Map& m = loaded_map();
		std::vector<std::string> types;
		if (params.contains("types")) {
			types = params["types"].get<std::vector<std::string>>();
		} else {
			types.push_back(required<std::string>(params, "type"));
		}
		const auto area = area_filter(m, params);
		if (!area) {
			throw Error(error_code::invalid_params, "Pass x, y, radius or left, bottom, right, top");
		}
		const bool circle = params.contains("radius");
		const glm::vec2 center(static_cast<float>(area->center().x()), static_cast<float>(area->center().y()));
		const float radius = static_cast<float>(area->width()) / 2.f;
		const float area_size = static_cast<float>(area->width() * area->height()) * 128.f * 128.f * (circle ? 0.785f : 1.f);
		const int count = params.contains("count") ? params["count"].get<int>()
												   : static_cast<int>(params.value("density", 10.f) * area_size / 1'000'000.f);
		const float min_distance = params.value("min_distance", 100.f) / 128.f;
		const bool avoid_water = params.value("avoid_water", true);
		const bool avoid_unwalkable = params.value("avoid_unwalkable", true);
		const std::array<float, 2> scale = params.value("scale", std::array<float, 2> {1.f, 1.f});

		struct Zone {
			std::vector<glm::vec2> points;
			float radius;
		};
		std::vector<Zone> zones;
		for (const auto& zone : params.value("avoid", nlohmann::json::array())) {
			if (zone.contains("points")) {
				Zone z {{}, zone.value("width", 300.f) / 2.f / 128.f};
				for (const auto& p : zone["points"]) {
					z.points.push_back(world_to_internal(m, glm::vec2(p[0].get<float>(), p[1].get<float>())));
				}
				zones.push_back(z);
			} else {
				zones.push_back({{world_to_internal(m, glm::vec2(zone["x"].get<float>(), zone["y"].get<float>()))}, zone["radius"].get<float>() / 128.f});
			}
		}
		const auto in_zone = [&](const glm::vec2 p) {
			for (const auto& zone : zones) {
				if (zone.points.size() == 1 && glm::distance(p, zone.points[0]) < zone.radius) {
					return true;
				}
				for (size_t i = 1; i < zone.points.size(); i++) {
					const glm::vec2 a = zone.points[i - 1];
					const glm::vec2 b = zone.points[i];
					const float t = std::clamp(glm::dot(p - a, b - a) / std::max(glm::dot(b - a, b - a), 0.0001f), 0.f, 1.f);
					if (glm::distance(p, a + t * (b - a)) < zone.radius) {
						return true;
					}
				}
			}
			return false;
		};

		// Existing doodads near the area count for the spacing, through a grid of tile sized buckets
		std::unordered_map<int64_t, std::vector<glm::vec2>> buckets;
		const auto bucket = [](const glm::vec2 p) {
			return (static_cast<int64_t>(std::floor(p.x)) << 32) ^ static_cast<int64_t>(static_cast<uint32_t>(std::floor(p.y)));
		};
		for (const auto& d : m.doodads.doodads) {
			buckets[bucket(glm::vec2(d.position))].push_back(glm::vec2(d.position));
		}
		const auto too_close = [&](const glm::vec2 p) {
			const int reach = static_cast<int>(std::ceil(min_distance));
			for (int dx = -reach; dx <= reach; dx++) {
				for (int dy = -reach; dy <= reach; dy++) {
					const auto found = buckets.find(bucket(p + glm::vec2(dx, dy)));
					if (found == buckets.end()) {
						continue;
					}
					for (const auto& other : found->second) {
						if (glm::distance(p, other) < min_distance) {
							return true;
						}
					}
				}
			}
			return false;
		};

		const PathingMap& pathing = m.pathing_map;
		std::mt19937 rng(params.contains("seed") ? params["seed"].get<uint32_t>() : std::random_device {}());
		std::uniform_real_distribution<float> unit(0.f, 1.f);
		auto edit = std::make_unique<DoodadEdit>();
		for (int attempt = 0; attempt < count * 30 && static_cast<int>(edit->added.size()) < count; attempt++) {
			glm::vec2 p;
			if (circle) {
				const float r = radius * std::sqrt(unit(rng));
				const float a = unit(rng) * 2.f * glm::pi<float>();
				p = center + glm::vec2(std::cos(a), std::sin(a)) * r;
			} else {
				p = glm::vec2(area->left() + unit(rng) * area->width(), area->top() + unit(rng) * area->height());
			}
			if (p.x < 1 || p.y < 1 || p.x >= m.terrain.width - 2 || p.y >= m.terrain.height - 2 || in_zone(p) || too_close(p)) {
				continue;
			}
			const glm::ivec2 cell = glm::ivec2(p * 4.f);
			const uint8_t flags = pathing.pathing_cells_static[cell.y * pathing.width + cell.x];
			if ((avoid_water && (flags & PathingMap::Flags::water)) || (avoid_unwalkable && (flags & PathingMap::Flags::unwalkable))) {
				continue;
			}

			const glm::vec2 world = internal_to_world(m, p);
			nlohmann::json entry = {
				{"type", types[rng() % types.size()]},
				{"x", world.x},
				{"y", world.y},
				{"facing", unit(rng) * 360.f},
				{"scale", scale[0] + unit(rng) * (scale[1] - scale[0])},
			};
			edit->added.push_back(make_doodad(m, entry));
			buckets[bucket(p)].push_back(p);
		}

		const size_t placed = edit->added.size();
		commit(m, std::move(edit));
		return {{"placed", placed}, {"requested", count}};
	}

	nlohmann::json doodads_update(const nlohmann::json& params) {
		Map& m = loaded_map();
		auto edit = std::make_unique<DoodadEdit>();
		for (const int id : id_list(params)) {
			const Doodad& before = find_doodad(m, id);
			Doodad after = before;
			apply_doodad_params(m, after, params);
			after.update(m.terrain);
			edit->changed.emplace_back(before, after);
		}
		nlohmann::json updated = nlohmann::json::array();
		for (const auto& [before, after] : edit->changed) {
			updated.push_back(doodad_json(m, after));
		}
		commit(m, std::move(edit));
		return params.contains("ids") ? nlohmann::json {{"doodads", updated}} : updated[0];
	}

	nlohmann::json doodads_delete(const nlohmann::json& params) {
		Map& m = loaded_map();
		auto edit = std::make_unique<DoodadEdit>();
		if (params.contains("type") || params.contains("radius") || params.contains("left")) {
			// Delete by filter
			const auto area = area_filter(m, params);
			const std::string type = params.value("type", "");
			for (const auto& d : m.doodads.doodads) {
				if ((type.empty() || d.id == type) && (!area || area->contains(d.position.x, d.position.y))) {
					edit->removed.push_back(d);
				}
			}
		} else {
			for (const int id : id_list(params)) {
				edit->removed.push_back(find_doodad(m, id));
			}
		}
		const size_t count = edit->removed.size();
		commit(m, std::move(edit));
		return {{"deleted", count}};
	}

	// ---------------------------------------------------------------- Units and items

	nlohmann::json unit_json(const Map& m, const Unit& u, const bool item) {
		const glm::vec2 world = internal_to_world(m, glm::vec2(u.position));
		nlohmann::json result = {
			{"id", u.creation_number},
			{"type", u.id},
			{"skin", u.skin_id},
			{"x", world.x},
			{"y", world.y},
			{"z", u.position.z * 128.f},
			{"facing", glm::degrees(u.angle)},
			{"player", u.player},
			{"variation", u.variation},
			{"item_drops", item_sets_json(u.item_sets)},
		};
		if (item) {
			return result;
		}

		result["hp"] = u.health;
		result["mana"] = u.mana;
		result["level"] = u.level;
		result["strength"] = u.strength;
		result["agility"] = u.agility;
		result["intelligence"] = u.intelligence;
		result["gold"] = u.gold;
		result["target_acquisition"] = u.target_acquisition;
		result["custom_color"] = u.custom_color;
		result["waygate"] = u.waygate;
		result["scale"] = {u.scale.x, u.scale.y, u.scale.z};
		nlohmann::json inventory = nlohmann::json::array();
		for (const auto& [slot, id] : u.items) {
			inventory.push_back({{"slot", slot}, {"type", id}});
		}
		result["inventory"] = inventory;
		nlohmann::json abilities = nlohmann::json::array();
		for (const auto& [id, autocast, level] : u.abilities) {
			abilities.push_back({{"type", id}, {"autocast", autocast != 0}, {"level", level}});
		}
		result["abilities"] = abilities;
		return result;
	}

	std::vector<Unit>& unit_list(Map& m, const bool items) {
		return items ? m.units.items : m.units.units;
	}

	Unit& find_unit(Map& m, const int id, const bool items) {
		for (auto& i : unit_list(m, items)) {
			if (i.creation_number == id) {
				return i;
			}
		}
		throw Error(error_code::invalid_params, std::format("No {} with id {}", items ? "item" : "unit", id));
	}

	void apply_unit_params(const Map& m, Unit& u, const nlohmann::json& params) {
		if (params.contains("x") || params.contains("y")) {
			const glm::vec2 current = internal_to_world(m, glm::vec2(u.position));
			const glm::vec2 internal = world_to_internal(m, glm::vec2(params.value("x", current.x), params.value("y", current.y)));
			u.position = glm::vec3(internal, m.terrain.interpolated_height(internal.x, internal.y, true));
		}
		if (params.contains("facing")) {
			u.angle = glm::radians(params["facing"].get<float>());
		}
		if (params.contains("scale")) {
			u.scale = params["scale"].is_array() ? glm::vec3(params["scale"][0], params["scale"][1], params["scale"][2]) : glm::vec3(params["scale"].get<float>());
		}
		const auto set_int = [&](const char* name, int& field) {
			if (params.contains(name)) {
				field = params[name].get<int>();
			}
		};
		set_int("player", u.player);
		set_int("variation", u.variation);
		set_int("hp", u.health);
		set_int("mana", u.mana);
		set_int("level", u.level);
		set_int("strength", u.strength);
		set_int("agility", u.agility);
		set_int("intelligence", u.intelligence);
		set_int("gold", u.gold);
		set_int("custom_color", u.custom_color);
		set_int("waygate", u.waygate);
		if (params.contains("target_acquisition")) {
			u.target_acquisition = params["target_acquisition"].get<float>();
		}
		if (params.contains("skin")) {
			u.skin_id = params["skin"].get<std::string>();
		}
		if (params.contains("item_drops")) {
			u.item_sets = parse_item_sets(params["item_drops"]);
			u.item_table_pointer = -1;
		}
		if (params.contains("inventory")) {
			u.items.clear();
			for (const auto& item : params["inventory"]) {
				u.items.emplace_back(item.value("slot", static_cast<uint32_t>(u.items.size())), required<std::string>(item, "type"));
			}
		}
		if (params.contains("abilities")) {
			u.abilities.clear();
			for (const auto& ability : params["abilities"]) {
				u.abilities.emplace_back(required<std::string>(ability, "type"), ability.value("autocast", false) ? 1u : 0u, ability.value("level", 1u));
			}
		}
	}

	nlohmann::json units_list_impl(const nlohmann::json& params, const bool items) {
		Map& m = loaded_map();
		const auto area = area_filter(m, params);
		const std::string type = params.value("type", "");
		const int player = params.value("player", -1);
		const size_t limit = params.value("limit", 200);

		nlohmann::json result = nlohmann::json::array();
		size_t total = 0;
		for (const auto& u : unit_list(m, items)) {
			if ((!type.empty() && u.id != type) || (player >= 0 && u.player != player) || (area && !area->contains(u.position.x, u.position.y))) {
				continue;
			}
			if (total++ < limit) {
				result.push_back(unit_json(m, u, items));
			}
		}
		return {{"total", total}, {items ? "items" : "units", result}};
	}

	nlohmann::json units_create_impl(const nlohmann::json& params, const bool items) {
		Map& m = loaded_map();
		const char* key = items ? "items" : "units";
		const nlohmann::json list = params.contains(key) ? params[key] : nlohmann::json::array({params});

		auto edit = std::make_unique<UnitEdit>();
		edit->items = items;
		for (const auto& entry : list) {
			const std::string type = required<std::string>(entry, "type");
			if (type == "sloc") {
				throw Error(error_code::invalid_params, "Start locations are set per player with players.update");
			}
			if (!(items ? items_slk : units_slk).row_headers.contains(type)) {
				throw Error(error_code::invalid_params, std::format("Unknown {} type {}", items ? "item" : "unit", type));
			}
			required<float>(entry, "x");
			required<float>(entry, "y");

			Unit u;
			u.id = type;
			u.skin_id = type;
			u.mesh = m.units.get_mesh(type);
			u.scale = glm::vec3(1.f);
			u.angle = glm::radians(270.f);
			u.random = {1, 0, 0, 0};
			u.creation_number = ++Unit::auto_increment;
			apply_unit_params(m, u, entry);
			u.skeleton = Skeleton(u.mesh->mdx, Units::get_required_animation_names(type));
			u.update();
			edit->added.push_back(u);
		}

		nlohmann::json created = nlohmann::json::array();
		for (const auto& u : edit->added) {
			created.push_back(unit_json(m, u, items));
		}
		commit(m, std::move(edit));
		return list.size() == 1 && !params.contains(key) ? created[0] : nlohmann::json {{key, created}};
	}

	nlohmann::json units_update_impl(const nlohmann::json& params, const bool items) {
		Map& m = loaded_map();
		auto edit = std::make_unique<UnitEdit>();
		edit->items = items;
		for (const int id : id_list(params)) {
			const Unit& before = find_unit(m, id, items);
			Unit after = before;
			apply_unit_params(m, after, params);
			after.update();
			edit->changed.emplace_back(before, after);
		}
		nlohmann::json updated = nlohmann::json::array();
		for (const auto& [before, after] : edit->changed) {
			updated.push_back(unit_json(m, after, items));
		}
		commit(m, std::move(edit));
		return params.contains("ids") ? nlohmann::json {{items ? "items" : "units", updated}} : updated[0];
	}

	nlohmann::json units_delete_impl(const nlohmann::json& params, const bool items) {
		Map& m = loaded_map();
		auto edit = std::make_unique<UnitEdit>();
		edit->items = items;
		if (params.contains("type") || params.contains("radius") || params.contains("left") || params.contains("player")) {
			const auto area = area_filter(m, params);
			const std::string type = params.value("type", "");
			const int player = params.value("player", -1);
			for (const auto& u : unit_list(m, items)) {
				if (u.id != "sloc" && (type.empty() || u.id == type) && (player < 0 || u.player == player)
					&& (!area || area->contains(u.position.x, u.position.y))) {
					edit->removed.push_back(u);
				}
			}
		} else {
			for (const int id : id_list(params)) {
				edit->removed.push_back(find_unit(m, id, items));
			}
		}
		const size_t count = edit->removed.size();
		commit(m, std::move(edit));
		return {{"deleted", count}};
	}

	nlohmann::json units_list(const nlohmann::json& params) {
		return units_list_impl(params, false);
	}

	nlohmann::json units_get(const nlohmann::json& params) {
		Map& m = loaded_map();
		return unit_json(m, find_unit(m, required<int>(params, "id"), false), false);
	}

	nlohmann::json units_create(const nlohmann::json& params) {
		return units_create_impl(params, false);
	}

	nlohmann::json units_update(const nlohmann::json& params) {
		return units_update_impl(params, false);
	}

	nlohmann::json units_delete(const nlohmann::json& params) {
		return units_delete_impl(params, false);
	}

	nlohmann::json items_list(const nlohmann::json& params) {
		return units_list_impl(params, true);
	}

	nlohmann::json items_create(const nlohmann::json& params) {
		return units_create_impl(params, true);
	}

	nlohmann::json items_update(const nlohmann::json& params) {
		return units_update_impl(params, true);
	}

	nlohmann::json items_delete(const nlohmann::json& params) {
		return units_delete_impl(params, true);
	}

	// ---------------------------------------------------------------- Regions

	nlohmann::json region_json(const Map& m, const Region& r) {
		const glm::vec2 min = internal_to_world(m, glm::vec2(r.left, r.bottom));
		const glm::vec2 max = internal_to_world(m, glm::vec2(r.right, r.top));
		return {
			{"id", r.creation_number},
			{"name", r.name},
			{"left", min.x},
			{"bottom", min.y},
			{"right", max.x},
			{"top", max.y},
			{"color", {r.color.r, r.color.g, r.color.b}},
			{"weather", r.weather_id},
			{"ambient_sound", r.ambient_id},
		};
	}

	void apply_region_params(const Map& m, Region& r, const nlohmann::json& params) {
		if (params.contains("name")) {
			r.name = params["name"].get<std::string>();
		}
		const glm::vec2 min_world = internal_to_world(m, glm::vec2(r.left, r.bottom));
		const glm::vec2 max_world = internal_to_world(m, glm::vec2(r.right, r.top));
		const glm::vec2 min = world_to_internal(m, glm::vec2(params.value("left", min_world.x), params.value("bottom", min_world.y)));
		const glm::vec2 max = world_to_internal(m, glm::vec2(params.value("right", max_world.x), params.value("top", max_world.y)));
		r.left = std::min(min.x, max.x);
		r.right = std::max(min.x, max.x);
		r.bottom = std::min(min.y, max.y);
		r.top = std::max(min.y, max.y);
		if (params.contains("color")) {
			r.color = glm::u8vec4(params["color"][0].get<int>(), params["color"][1].get<int>(), params["color"][2].get<int>(), 255);
		}
		if (params.contains("weather")) {
			r.weather_id = params["weather"].get<std::string>();
		}
		if (params.contains("ambient_sound")) {
			r.ambient_id = params["ambient_sound"].get<std::string>();
		}
	}

	Region* find_region(Map& m, const nlohmann::json& params) {
		for (auto& r : m.regions.regions) {
			if ((params.contains("id") && r.creation_number == params["id"].get<int>()) || (params.contains("name") && !params.contains("id") && r.name == params["name"].get<std::string>())) {
				return &r;
			}
		}
		throw Error(error_code::invalid_params, "No such region, pass its id or name");
	}

	void check_unique_name(const Map& m, const std::string& name, const int except) {
		for (const auto& r : m.regions.regions) {
			if (r.name == name && r.creation_number != except) {
				throw Error(error_code::invalid_params, "A region named " + name + " already exists");
			}
		}
	}

	nlohmann::json regions_list(const nlohmann::json&) {
		const Map& m = loaded_map();
		nlohmann::json result = nlohmann::json::array();
		for (const auto& r : m.regions.regions) {
			result.push_back(region_json(m, r));
		}
		return result;
	}

	nlohmann::json regions_create(const nlohmann::json& params) {
		Map& m = loaded_map();
		for (const char* name : {"left", "bottom", "right", "top"}) {
			required<float>(params, name);
		}
		int next = 0;
		for (const auto& r : m.regions.regions) {
			next = std::max(next, r.creation_number + 1);
		}
		Region region;
		region.creation_number = next;
		region.name = std::format("Region {:03}", next);
		region.block_camera = 0;
		region.alpha_tile_minimap_color = 0;
		apply_region_params(m, region, params);
		check_unique_name(m, region.name, region.creation_number);

		auto edit = std::make_unique<RegionsEdit>();
		edit->before = m.regions.regions;
		edit->after = m.regions.regions;
		edit->after.push_back(region);
		commit(m, std::move(edit));
		return region_json(m, region);
	}

	nlohmann::json regions_update(const nlohmann::json& params) {
		Map& m = loaded_map();
		auto edit = std::make_unique<RegionsEdit>();
		edit->before = m.regions.regions;
		Region* region = find_region(m, params);
		Region updated = *region;
		nlohmann::json changes = params;
		if (changes.contains("id")) {
			// "name" is a rename when the region is found by id
			changes.erase("id");
		} else {
			changes.erase("name");
			if (params.contains("new_name")) {
				changes["name"] = params["new_name"];
			}
		}
		apply_region_params(m, updated, changes);
		check_unique_name(m, updated.name, updated.creation_number);
		*region = updated;
		edit->after = m.regions.regions;
		m.regions.regions = edit->before;
		commit(m, std::move(edit));
		return region_json(m, updated);
	}

	nlohmann::json regions_delete(const nlohmann::json& params) {
		Map& m = loaded_map();
		const Region* region = find_region(m, params);
		const int number = region->creation_number;
		auto edit = std::make_unique<RegionsEdit>();
		edit->before = m.regions.regions;
		edit->after = m.regions.regions;
		std::erase_if(edit->after, [&](const Region& r) {
			return r.creation_number == number;
		});
		commit(m, std::move(edit));
		return {{"deleted", 1}};
	}

	// ---------------------------------------------------------------- Game cameras

	nlohmann::json game_camera_json(const Map& m, const GameCamera& c, const size_t index) {
		const glm::vec2 world = internal_to_world(m, glm::vec2(c.target_x, c.target_y));
		return {
			{"index", index},
			{"name", c.name},
			{"x", world.x},
			{"y", world.y},
			{"z_offset", c.z_offset * 128.f},
			{"rotation", c.rotation},
			{"angle_of_attack", c.angle_of_attack},
			{"distance", c.distance},
			{"roll", c.roll},
			{"fov", c.fov},
			{"far_z", c.far_z},
			{"near_z", c.near_z},
		};
	}

	void apply_game_camera_params(const Map& m, GameCamera& c, const nlohmann::json& params) {
		if (params.contains("name")) {
			c.name = params["name"].get<std::string>();
		}
		const glm::vec2 current = internal_to_world(m, glm::vec2(c.target_x, c.target_y));
		const glm::vec2 target = world_to_internal(m, glm::vec2(params.value("x", current.x), params.value("y", current.y)));
		c.target_x = target.x;
		c.target_y = target.y;
		if (params.contains("z_offset")) {
			// Stored in tiles like positions
			c.z_offset = params["z_offset"].get<float>() / 128.f;
		}
		for (const auto& [name, field] : {std::pair {"rotation", &c.rotation}, {"angle_of_attack", &c.angle_of_attack}, {"distance", &c.distance}, {"roll", &c.roll}, {"fov", &c.fov}, {"far_z", &c.far_z}, {"near_z", &c.near_z}}) {
			if (params.contains(name)) {
				*field = params[name].get<float>();
			}
		}
	}

	GameCamera& find_game_camera(Map& m, const nlohmann::json& params) {
		if (params.contains("index")) {
			const size_t index = params["index"].get<size_t>();
			if (index < m.cameras.cameras.size()) {
				return m.cameras.cameras[index];
			}
		} else if (params.contains("name")) {
			for (auto& c : m.cameras.cameras) {
				if (c.name == params["name"].get<std::string>()) {
					return c;
				}
			}
		}
		throw Error(error_code::invalid_params, "No such camera, pass its index or name");
	}

	nlohmann::json cameras_list(const nlohmann::json&) {
		const Map& m = loaded_map();
		nlohmann::json result = nlohmann::json::array();
		for (size_t i = 0; i < m.cameras.cameras.size(); i++) {
			result.push_back(game_camera_json(m, m.cameras.cameras[i], i));
		}
		return result;
	}

	nlohmann::json cameras_create(const nlohmann::json& params) {
		Map& m = loaded_map();
		GameCamera camera {};
		camera.name = std::format("Camera {:03}", m.cameras.cameras.size() + 1);
		// The game's defaults
		camera.rotation = 90.f;
		camera.angle_of_attack = 304.f;
		camera.distance = 1650.f;
		camera.fov = 70.f;
		camera.far_z = 5000.f;
		camera.near_z = 100.f;
		camera.local_pitch = 0.f;
		camera.local_yaw = 0.f;
		camera.local_roll = 0.f;
		camera.dof_distance = 0.f;
		camera.dof_scale = 0.f;
		camera.pos_absolute_z = 0.f;
		camera.free_camera = 0;
		required<float>(params, "x");
		required<float>(params, "y");
		apply_game_camera_params(m, camera, params);
		m.cameras.cameras.push_back(camera);
		return game_camera_json(m, camera, m.cameras.cameras.size() - 1);
	}

	nlohmann::json cameras_update(const nlohmann::json& params) {
		Map& m = loaded_map();
		GameCamera& camera = find_game_camera(m, params);
		nlohmann::json changes = params;
		if (!params.contains("index")) {
			changes.erase("name");
			if (params.contains("new_name")) {
				changes["name"] = params["new_name"];
			}
		}
		apply_game_camera_params(m, camera, changes);
		return game_camera_json(m, camera, &camera - m.cameras.cameras.data());
	}

	nlohmann::json cameras_delete(const nlohmann::json& params) {
		Map& m = loaded_map();
		const GameCamera& camera = find_game_camera(m, params);
		m.cameras.cameras.erase(m.cameras.cameras.begin() + (&camera - m.cameras.cameras.data()));
		return {{"deleted", 1}};
	}

	export std::vector<MethodEntry> object_methods() {
		return {
			{"doodads.list", doodads_list},
			{"doodads.get", doodads_get},
			{"doodads.create", doodads_create},
			{"doodads.update", doodads_update},
			{"doodads.delete", doodads_delete},
			{"doodads.scatter", doodads_scatter},
			{"doodads.models", doodads_models},
			{"units.list", units_list},
			{"units.get", units_get},
			{"units.create", units_create},
			{"units.update", units_update},
			{"units.delete", units_delete},
			{"items.list", items_list},
			{"items.create", items_create},
			{"items.update", items_update},
			{"items.delete", items_delete},
			{"regions.list", regions_list},
			{"regions.create", regions_create},
			{"regions.update", regions_update},
			{"regions.delete", regions_delete},
			{"cameras.list", cameras_list},
			{"cameras.create", cameras_create},
			{"cameras.update", cameras_update},
			{"cameras.delete", cameras_delete},
		};
	}
} // namespace api
