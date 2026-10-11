export module WorldUndoManager;

import std;
import PathingMap;
import Units;
import Doodads;
import Terrain;
import Regions;
import "brush/brush.h";

/// So we don't have to pass a global variable around
export struct WorldEditContext {
	Terrain& terrain;
	Units& units;
	Doodads& doodads;
	Regions& regions;
	Brush* brush;
	PathingMap& pathing_map;
};

/// Describes a change in the world. E.g., changing the terrain, adding a unit in the OE, placing some doodads.
export class WorldCommand {
  public:
	virtual void undo(WorldEditContext& ctx) = 0;
	virtual void redo(WorldEditContext& ctx) = 0;

	virtual ~WorldCommand() = default;
};

export class WorldUndoManager {
	std::vector<std::vector<std::unique_ptr<WorldCommand>>> undo_actions;
	std::vector<std::vector<std::unique_ptr<WorldCommand>>> redo_actions;

  public:
	void undo(WorldEditContext& ctx) {
		if (undo_actions.empty()) {
			return;
		}

		// Backwards, as a later action in the group may build on an earlier one
		auto& actions = undo_actions.back();
		for (auto i = actions.rbegin(); i != actions.rend(); ++i) {
			(*i)->undo(ctx);
		}

		redo_actions.push_back(std::move(actions));
		undo_actions.pop_back();
	}

	void redo(WorldEditContext& ctx) {
		if (redo_actions.empty()) {
			return;
		}

		auto& actions = redo_actions.back();
		for (const auto& i : actions) {
			i->redo(ctx);
		}

		undo_actions.push_back(std::move(actions));
		redo_actions.pop_back();
	}

	void new_undo_group() {
		undo_actions.push_back({});
	}

	void add_undo_action(std::unique_ptr<WorldCommand> action) {
		if (undo_actions.empty()) {
			return;
		}

		undo_actions.back().push_back(std::move(action));
		redo_actions.clear();
	}

	/// The number of undo groups, to pass to merge_groups_since() later
	size_t group_count() const {
		return undo_actions.size();
	}

	/// Merges the undo groups added since group_count() returned `mark` into one, so they undo as one step
	void merge_groups_since(const size_t mark) {
		if (undo_actions.size() <= mark + 1) {
			return;
		}
		auto& target = undo_actions[mark];
		for (size_t i = mark + 1; i < undo_actions.size(); i++) {
			for (auto& action : undo_actions[i]) {
				target.push_back(std::move(action));
			}
		}
		undo_actions.resize(mark + 1);
	}

	/// Removes all undo/redo operations
	void clear_all_undo() {
		undo_actions.clear();
		redo_actions.clear();
	}
};
