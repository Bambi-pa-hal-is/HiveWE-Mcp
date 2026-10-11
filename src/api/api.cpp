#include <QCoreApplication>
#include <QSettings>

#include "http_transport.h"
#include "mcp_panel.h"
// After the textual standard library includes, as it imports modules
#include "main_window/hivewe.h"
#include "main_window/map_widget.h"
#include "render/vulkan_viewport.h"
#include "api.h"

import std;
import Map;
import MapGlobal;
import WindowHandler;
import WorldUndoManager;
import "trigger_editor.h";
import ApiAssetMethods;
import ApiCommon;
import ApiEditorMethods;
import ApiHost;
import ApiMapInfoMethods;
import ApiMapMethods;
import ApiObjectDataMethods;
import ApiObjectMethods;
import ApiScriptMethods;
import ApiServer;
import ApiTerrainMethods;

namespace api {
	namespace {
		std::unique_ptr<Server> server;
	} // namespace

	void start(const unsigned short port, HiveWE& window) {
		host.load_map = [&window](const std::filesystem::path& directory) {
			window.load_map(directory);
		};
		host.set_window_title = [&window](const QString& title) {
			window.setWindowTitle(title);
		};
		host.saving_initiated = [&window] {
			emit window.saving_initiated();
		};
		host.close_trigger_editor = [] {
			if (const auto editor = window_handler.get_open<TriggerEditor>()) {
				editor.value()->save_changes();
				editor.value()->close();
			}
		};
		host.capture_viewport = [&window] {
			const MapWidget* widget = window.findChild<MapWidget*>();
			if (widget == nullptr || widget->viewport == nullptr) {
				return QImage();
			}
			return widget->viewport->capture();
		};

		server = std::make_unique<Server>();
		for (const auto& methods : {editor_methods(), map_methods(), terrain_methods(), object_methods(), object_data_methods(), map_info_methods(), script_methods(), asset_methods()}) {
			for (const auto& [name, method] : methods) {
				server->register_method(name, method);
			}
		}

		server->undo_mark = [] {
			return map ? map->world_undo.group_count() : 0;
		};
		server->undo_merge = [](const std::size_t mark) {
			if (map) {
				map->world_undo.merge_groups_since(mark);
			}
		};

		const bool started = http::start(port, [](const std::string& body) {
			return server->handle(body);
		});
		if (started) {
			QObject::connect(qApp, &QCoreApplication::aboutToQuit, [] {
				server->stop();
				http::stop();
				server.reset();
			});
		} else {
			server.reset();
		}

		// `--no-mcp` keeps the MCP server from starting, for when a client starts it itself over stdio
		const bool start_mcp = QSettings().value("mcp/autostart", true).toBool() && !QCoreApplication::arguments().contains("--no-mcp");
		install_mcp_panel(window, port, started, start_mcp);
	}
} // namespace api
