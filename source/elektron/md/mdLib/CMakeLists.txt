cmake_minimum_required(VERSION 3.10)

project(mdLib)

option(MD_TRANSPORT_DIAGNOSTICS
	"Compile the MD/MM transport and scheduler scorecard" OFF)

add_library(mdLib STATIC)

set(SOURCES
	mdautomation.cpp mdautomation.h
	mdautomationsync.cpp mdautomationsync.h
	mddevice.cpp mddevice.h
	mddsp.cpp mddsp.h
	mdflash.cpp mdflash.h
	mdfrontpanel.cpp mdfrontpanel.h
	mdhardware.cpp mdhardware.h
	mdhostclock.h
	mdmc.cpp mdmc.h
	mdmemorymap.h
	mdmidiprotocol.h
	mdsysexautomation.cpp mdsysexautomation.h
	mdrealtimemidiqueue.h
	mdpanel.cpp mdpanel.h
	mdsdsencode.cpp mdsdsencode.h
	mdsampledirectory.cpp mdsampledirectory.h
	mdmachines.cpp mdmachines.h
	mdrampacking.h
	mdsim.cpp mdsim.h
	mdrom.cpp mdrom.h
	mdromdata.cpp mdromdata.h
	mdromloader.cpp mdromloader.h
	mdscheduledmidi.h
	mdstate.cpp mdstate.h
	mdscene.cpp mdscene.h
	mdsysextransfer.h
	mdsyseximport.h
	mdturbomidi.cpp mdturbomidi.h mdturbomidiprotocol.h mdturbomidisenderpolicy.h
	mdtypes.h
	mdtimedhostrx.h
	mdtransportdiagnostics.h
	mdtransportpolicy.h
)

target_sources(mdLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(mdLib PUBLIC hardwareLib dsp56kEmu)
target_compile_definitions(mdLib PUBLIC
	MD_TRANSPORT_DIAGNOSTICS=$<BOOL:${MD_TRANSPORT_DIAGNOSTICS}>)

set_property(TARGET mdLib PROPERTY FOLDER "Elektron")

# The remote front panel's web server owns sockets, threads and files, so it is a library of its
# own: the device core (mdLib, and libgm_md built on it) needs none of that. The plug-in links it;
# hosts of libgm_md use libgm_md_panel (mdPanelService).
add_library(mdRemotePanel STATIC mdremotepanel.cpp mdremotepanel.h)
target_link_libraries(mdRemotePanel PUBLIC mdLib networkLib)
set_property(TARGET mdRemotePanel PROPERTY FOLDER "Elektron")

target_include_directories(mdLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Scheduling decisions must use the same binary64 operations on every host.
# Fused/reassociated frame arithmetic can cross an integer cycle boundary and
# change the order of emulated processors. Keep this restriction local to the
# scheduler translation unit; audio/JIT arithmetic keeps its existing options.
if(MSVC)
	set_property(SOURCE mdhardware.cpp APPEND PROPERTY COMPILE_OPTIONS /fp:strict)
else()
	set_property(SOURCE mdhardware.cpp APPEND PROPERTY COMPILE_OPTIONS
		-fno-fast-math -ffp-contract=off)
endif()
