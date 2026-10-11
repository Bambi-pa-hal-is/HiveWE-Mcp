find_package(httplib CONFIG REQUIRED)

target_sources(HiveWE_lib PUBLIC FILE_SET api_modules TYPE CXX_MODULES FILES
	"api_error.ixx"
	"api_server.ixx"
	"api_host.ixx"
	"api_common.ixx"
	"api_editor.ixx"
	"api_map.ixx"
	"api_terrain.ixx"
	"api_objects.ixx"
	"api_objectdata.ixx"
	"api_mapinfo.ixx"
	"api_script.ixx"
	"api_assets.ixx"
)

target_sources(HiveWE_lib PRIVATE
	"api.cpp"
	"api.h"
	"http_transport.cpp"
	"http_transport.h"
	"mcp_panel.cpp"
	"mcp_panel.h"
)

target_link_libraries(HiveWE_lib PRIVATE httplib::httplib)
