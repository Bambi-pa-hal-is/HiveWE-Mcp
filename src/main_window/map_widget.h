#pragma once

#include <QWidget>

class VulkanViewport;

/// Hosts the Vulkan map viewport in the main window. The viewport is a native window, so widgets
/// shown on top of it (like the minimap) must be native too.
class MapWidget : public QWidget {
	Q_OBJECT

  public:
	explicit MapWidget(QWidget* parent = nullptr);

	/// The map viewport, or null when Vulkan is unavailable
	VulkanViewport* viewport = nullptr;
};
