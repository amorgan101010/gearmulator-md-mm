if(NOT DEFINED APP_SOURCE OR NOT EXISTS "${APP_SOURCE}")
	message(FATAL_ERROR "native-panel ownership source path is missing")
endif()

file(READ "${APP_SOURCE}" app)
foreach(forbidden
	"gm_device_create"
	"AudioPluginAudioProcessor"
	"AudioDeviceManager"
	"MidiInput::"
	"MidiOutput::"
	"RomLoader::"
	"md::Device"
	"juce_plugin_modules"
)
	string(FIND "${app}" "${forbidden}" found)
	if(NOT found EQUAL -1)
		message(FATAL_ERROR "native panel frontend must not own ${forbidden}")
	endif()
endforeach()

file(READ "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" targets)
string(FIND "${targets}" "target_link_libraries(mdNativePanelStandalone PRIVATE\n\tjuceRmlUi juceUiLib mdLib jucePluginData mdNativePanel_BinaryData" target_found)
if(target_found EQUAL -1)
	message(FATAL_ERROR "native panel target no longer has its processor-free link boundary")
endif()
message(STATUS "native panel construction is restricted to JUCE GUI/RmlUi, copied panel IPC, and panel protocol code")
