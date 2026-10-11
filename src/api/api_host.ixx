module;

#include <QImage>
#include <QString>

export module ApiHost;

import std;

export namespace api {
	/// What the API needs from the main window, filled in by api.cpp
	struct EditorHost {
		/// Loads a folder map the way File > Open does
		std::function<void(const std::filesystem::path&)> load_map;
		std::function<void(const QString&)> set_window_title;
		/// Lets open editors write their pending changes into the map before it is saved
		std::function<void()> saving_initiated;
		/// Renders the map viewport and returns the frame, or a null image when the viewport can't render
		std::function<QImage()> capture_viewport;
		/// Writes pending trigger editor edits into the map and closes it, so it can't overwrite API changes later
		std::function<void()> close_trigger_editor;
	};

	inline EditorHost host;
} // namespace api
