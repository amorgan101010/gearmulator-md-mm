cmake_minimum_required(VERSION 3.15)

# libgm_md: the Machinedrum/Monomachine core behind the Gearmulator device ABI
# (source/gmDeviceAbi/gm_device.h), for in-process hosts such as the studio engine.
# Only the gm_* functions are exported; everything from mdLib and below stays internal.
add_library(gm_md SHARED gmMdDevice.cpp ../../../gmDeviceAbi/gm_device.h)
target_link_libraries(gm_md PRIVATE mdLib)
target_include_directories(gm_md PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_target_properties(gm_md PROPERTIES
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
	FOLDER "Elektron")
if(UNIX AND NOT APPLE)
	target_link_options(gm_md PRIVATE
		"-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/gm_md.map" "-Wl,--no-undefined")
	set_property(TARGET gm_md APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gm_md.map)
endif()

if(BUILD_TESTING)
	# Written in C on purpose: proves the header is plain C and drives the library only through it.
	add_executable(gmMdDeviceTest gmMdDeviceTest.c)
	target_link_libraries(gmMdDeviceTest PRIVATE gm_md)
	if(UNIX)
		target_link_libraries(gmMdDeviceTest PRIVATE m)
	endif()
	set_target_properties(gmMdDeviceTest PROPERTIES FOLDER "Elektron/test")
	add_test(NAME gmMdDeviceTest COMMAND gmMdDeviceTest)
	# Local tool (not a test): how often the state changes during playback, and what get_state costs.
	add_executable(gmStateProbe gmStateProbe.cpp)
	target_link_libraries(gmStateProbe PRIVATE gm_md ${CMAKE_DL_LIBS})
	set_target_properties(gmStateProbe PROPERTIES FOLDER "Elektron/test")
	# 77: no firmware configured.
	set_tests_properties(gmMdDeviceTest PROPERTIES LABELS "Firmware" SKIP_RETURN_CODE 77)
endif()
