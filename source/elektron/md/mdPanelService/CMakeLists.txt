cmake_minimum_required(VERSION 3.15)

# libgm_md_panel: the Machinedrum/Monomachine web front panel as a host service (gm_md_panel.h).
# It holds the sockets, threads and files that libgm_md does not; it talks to a unit only through
# the host's callbacks, which map onto the device ABI.
add_library(gm_md_panel SHARED gmMdPanel.cpp gm_md_panel.h)
target_link_libraries(gm_md_panel PRIVATE mdRemotePanel)
target_include_directories(gm_md_panel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_target_properties(gm_md_panel PROPERTIES
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
	FOLDER "Elektron")
if(UNIX AND NOT APPLE)
	target_link_options(gm_md_panel PRIVATE
		"-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/gm_md_panel.map" "-Wl,--no-undefined")
	set_property(TARGET gm_md_panel APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gm_md_panel.map)
endif()
