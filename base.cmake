if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(MSVC)
	# https://cmake.org/cmake/help/latest/variable/CMAKE_MSVC_RUNTIME_LIBRARY.html#variable:CMAKE_MSVC_RUNTIME_LIBRARY
	cmake_policy(SET CMP0091 NEW)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} /IGNORE:4221")

	# /O2 Full Optimization (Favor Speed)
	# /GS- disable security checks
	# /fp:fast
	# /Oy omit frame pointers
	# /GT enable fiber-safe optimizations
	# /GL Whole Program Optimization
	# /Zi Generate Debug Info PDB
	# /Oi Enable Intrinsic Functions
	# /Ot Favor Fast Code
	# /permissive- Standards Conformance
	# /MP Multiprocessor Compilation

	option(GEARMULATOR_MSVC_EMBED_DEBUG_INFO
		"Use /Z7 embedded debug information for cache-friendly MSVC builds" OFF)
	if(GEARMULATOR_MSVC_EMBED_DEBUG_INFO)
		set(GEARMULATOR_MSVC_DEBUG_INFO_FLAG "/Z7")
	else()
		# /MP can otherwise race multiple compiler processes against one PDB.
		set(GEARMULATOR_MSVC_DEBUG_INFO_FLAG "/Zi /FS")
	endif()

	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /GS- /fp:fast /Oy /GT /GL ${GEARMULATOR_MSVC_DEBUG_INFO_FLAG} /Oi /Ot")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /GS- /fp:fast /Oy /GT /GL ${GEARMULATOR_MSVC_DEBUG_INFO_FLAG} /Oi /Ot")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /permissive- /MP")

	set(ARCHITECTURE ${CMAKE_VS_PLATFORM_NAME})

	if(NOT ${CMAKE_VS_PLATFORM_NAME} STREQUAL "x64")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /arch:SSE2")
	endif()

	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /D_CRT_SECURE_NO_WARNINGS")

	set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE} /LTCG")
	set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} /LTCG /DEBUG")
	set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} /LTCG /DEBUG")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG /DEBUG")

	set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /SAFESEH:NO")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
elseif(APPLE)
#	set(ARCHITECTURE ${CMAKE_OSX_ARCHITECTURES})
	set(ARCHITECTURE "MacOS")
	set(OS_LINK_LIBRARIES
	    "-framework Accelerate"
	    "-framework ApplicationServices"
	    "-framework AudioUnit"
	    "-framework AudioToolbox"
	    "-framework Carbon"
	    "-framework CoreAudio"
	    "-framework CoreAudioKit"
	    "-framework CoreServices"
	    "-framework CoreText"
	    "-framework Cocoa"
	    "-framework CoreFoundation"
	    "-framework OpenGL"
	    "-framework QuartzCore"  	
	)
	# Keep release builds linkable within bounded disk and memory budgets.
	string(APPEND CMAKE_C_FLAGS_RELEASE " -funroll-loops -Ofast -fno-lto -fno-stack-protector")
	string(APPEND CMAKE_CXX_FLAGS_RELEASE " -funroll-loops -Ofast -fno-lto -fno-stack-protector")
else()
	message("CMAKE_SYSTEM_PROCESSOR: " ${CMAKE_SYSTEM_PROCESSOR})
	message("CMAKE_HOST_SYSTEM_PROCESSOR: " ${CMAKE_HOST_SYSTEM_PROCESSOR})

	if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES arm AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES aarch64)
		string(APPEND CMAKE_CXX_FLAGS " -msse")
	endif()

	option(GEARMULATOR_ENABLE_GCC_LTO
		"Enable GCC link-time optimization for release builds" OFF)
	set(GEARMULATOR_GCC_PGO "off" CACHE STRING
		"GCC profile-guided optimization for release builds: off, generate or use")
	set_property(CACHE GEARMULATOR_GCC_PGO PROPERTY STRINGS off generate use)
	set(GEARMULATOR_GCC_PGO_DIR "${CMAKE_BINARY_DIR}/pgo-data" CACHE PATH
		"Where GCC writes and reads the PGO profile. Generate and use builds must share it")
	option(GEARMULATOR_ENABLE_GCC_NATIVE_TUNING
		"Optimize GCC release builds for the build host CPU" OFF)
	option(GEARMULATOR_ENABLE_CLANG_THINLTO
		"Compatibility alias: enable Linux Clang ThinLTO for release builds" OFF)
	set(GEARMULATOR_CLANG_LTO "off" CACHE STRING
		"Clang link-time optimization for release builds: off, thin or full")
	set_property(CACHE GEARMULATOR_CLANG_LTO PROPERTY STRINGS off thin full)
	set(GEARMULATOR_CLANG_PGO "off" CACHE STRING
		"Clang instrumentation PGO for release builds: off, generate or use")
	set_property(CACHE GEARMULATOR_CLANG_PGO PROPERTY STRINGS off generate use)
	set(GEARMULATOR_CLANG_PGO_PROFILE "" CACHE FILEPATH
		"Merged LLVM profile used by Clang PGO")

	# GCC LTO remains opt-in because not every product has completed the same
	# validation as the Monomachine path. VirusProcessor.cpp carries its own
	# source-level -fno-lto workaround.
	if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		if(GEARMULATOR_ENABLE_GCC_NATIVE_TUNING)
			message(STATUS "GCC native CPU tuning enabled")
			string(APPEND CMAKE_C_FLAGS_RELEASE " -march=native -mtune=native")
			string(APPEND CMAKE_CXX_FLAGS_RELEASE " -march=native -mtune=native")
		endif()

		# -Wno-error=coverage-mismatch: a source edited after profiling just loses its profile, instead of
		# failing the build.
		# Profile files are named after each object's path. Without a prefix they carry the build
		set(_gearmulatorPgoPrefix "-fprofile-prefix-path=${CMAKE_BINARY_DIR}")
		if(GEARMULATOR_GCC_PGO STREQUAL "generate")
			message(STATUS "GCC PGO: instrumented build, profile goes to ${GEARMULATOR_GCC_PGO_DIR}")
			# -fprofile-update=single: the scheduler runs on one thread, so non-atomic counters suffice.
			string(APPEND CMAKE_C_FLAGS_RELEASE " -fprofile-generate=${GEARMULATOR_GCC_PGO_DIR} ${_gearmulatorPgoPrefix} -fprofile-update=single")
			string(APPEND CMAKE_CXX_FLAGS_RELEASE " -fprofile-generate=${GEARMULATOR_GCC_PGO_DIR} ${_gearmulatorPgoPrefix} -fprofile-update=single")
			string(APPEND CMAKE_EXE_LINKER_FLAGS " -fprofile-generate=${GEARMULATOR_GCC_PGO_DIR}")
			string(APPEND CMAKE_SHARED_LINKER_FLAGS " -fprofile-generate=${GEARMULATOR_GCC_PGO_DIR}")
		elseif(GEARMULATOR_GCC_PGO STREQUAL "use")
			if(NOT EXISTS "${GEARMULATOR_GCC_PGO_DIR}")
				message(FATAL_ERROR
					"GEARMULATOR_GCC_PGO=use needs a profile in ${GEARMULATOR_GCC_PGO_DIR}; run a generate build first")
			endif()
			message(STATUS "GCC PGO: using the profile in ${GEARMULATOR_GCC_PGO_DIR}")
			string(APPEND CMAKE_C_FLAGS_RELEASE " -fprofile-use=${GEARMULATOR_GCC_PGO_DIR} ${_gearmulatorPgoPrefix} -fprofile-correction -Wno-missing-profile -Wno-error=coverage-mismatch")
			string(APPEND CMAKE_CXX_FLAGS_RELEASE " -fprofile-use=${GEARMULATOR_GCC_PGO_DIR} ${_gearmulatorPgoPrefix} -fprofile-correction -Wno-missing-profile -Wno-error=coverage-mismatch")
		endif()
		if(GEARMULATOR_ENABLE_GCC_LTO)
			message(STATUS "GCC LTO enabled")
			string(APPEND CMAKE_C_FLAGS_RELEASE " -flto=auto")
			string(APPEND CMAKE_CXX_FLAGS_RELEASE " -flto=auto")
		else()
			message(STATUS
				"GCC LTO disabled; enable GEARMULATOR_ENABLE_GCC_LTO after product validation")
		endif()
	else()
		if(CMAKE_C_COMPILER_ID STREQUAL "Clang"
			AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
			# Keep the old boolean usable, but make compiler, LTO and PGO
			# independently selectable for reproducible comparisons.
			if(GEARMULATOR_ENABLE_CLANG_THINLTO
				AND GEARMULATOR_CLANG_LTO STREQUAL "off")
				set(GEARMULATOR_CLANG_LTO "thin")
			endif()
			if(NOT GEARMULATOR_CLANG_LTO MATCHES "^(off|thin|full)$")
				message(FATAL_ERROR "GEARMULATOR_CLANG_LTO must be off, thin, or full")
			endif()
			if(NOT GEARMULATOR_CLANG_PGO MATCHES "^(off|generate|use)$")
				message(FATAL_ERROR "GEARMULATOR_CLANG_PGO must be off, generate, or use")
			endif()
			if(NOT GEARMULATOR_CLANG_LTO STREQUAL "off")
				message(STATUS "Clang ${GEARMULATOR_CLANG_LTO} LTO enabled")
				if(GEARMULATOR_CLANG_LTO STREQUAL "thin")
					string(APPEND CMAKE_C_FLAGS_RELEASE " -flto=thin")
					string(APPEND CMAKE_CXX_FLAGS_RELEASE " -flto=thin")
					string(APPEND CMAKE_EXE_LINKER_FLAGS " -flto=thin -fuse-ld=lld")
					string(APPEND CMAKE_SHARED_LINKER_FLAGS " -flto=thin -fuse-ld=lld")
				else()
					string(APPEND CMAKE_C_FLAGS_RELEASE " -flto")
					string(APPEND CMAKE_CXX_FLAGS_RELEASE " -flto")
					string(APPEND CMAKE_EXE_LINKER_FLAGS " -flto -fuse-ld=lld")
					string(APPEND CMAKE_SHARED_LINKER_FLAGS " -flto -fuse-ld=lld")
				endif()
			endif()
			if(GEARMULATOR_CLANG_PGO STREQUAL "generate")
				message(STATUS "Clang PGO: instrumented build")
				string(APPEND CMAKE_C_FLAGS_RELEASE " -fprofile-instr-generate")
				string(APPEND CMAKE_CXX_FLAGS_RELEASE " -fprofile-instr-generate")
				string(APPEND CMAKE_EXE_LINKER_FLAGS " -fprofile-instr-generate")
				string(APPEND CMAKE_SHARED_LINKER_FLAGS " -fprofile-instr-generate")
			elseif(GEARMULATOR_CLANG_PGO STREQUAL "use")
				if(NOT EXISTS "${GEARMULATOR_CLANG_PGO_PROFILE}"
					OR IS_DIRECTORY "${GEARMULATOR_CLANG_PGO_PROFILE}")
					message(FATAL_ERROR "GEARMULATOR_CLANG_PGO_PROFILE must name an existing merged LLVM profile")
				endif()
				message(STATUS "Clang PGO: using ${GEARMULATOR_CLANG_PGO_PROFILE}")
				string(APPEND CMAKE_C_FLAGS_RELEASE " -fprofile-instr-use=${GEARMULATOR_CLANG_PGO_PROFILE} -Wno-profile-instr-out-of-date")
				string(APPEND CMAKE_CXX_FLAGS_RELEASE " -fprofile-instr-use=${GEARMULATOR_CLANG_PGO_PROFILE} -Wno-profile-instr-out-of-date")
				string(APPEND CMAKE_EXE_LINKER_FLAGS " -fprofile-instr-use=${GEARMULATOR_CLANG_PGO_PROFILE}")
				string(APPEND CMAKE_SHARED_LINKER_FLAGS " -fprofile-instr-use=${GEARMULATOR_CLANG_PGO_PROFILE}")
			endif()
		elseif(GEARMULATOR_ENABLE_CLANG_THINLTO)
			message(FATAL_ERROR "GEARMULATOR_ENABLE_CLANG_THINLTO requires Clang on Linux")
		else()
			cmake_policy(SET CMP0069 NEW)
			include(CheckIPOSupported)

			check_ipo_supported(RESULT result)
			if(result)
				message(STATUS "IPO is supported")
				set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
			else()
				message(WARNING "IPO is not supported")
			endif()
		endif()
	endif()

	string(APPEND CMAKE_C_FLAGS_RELEASE " -Ofast -fno-stack-protector")
	string(APPEND CMAKE_CXX_FLAGS_RELEASE " -Ofast -fno-stack-protector")
	string(APPEND CMAKE_CXX_FLAGS_DEBUG " -rdynamic")

	execute_process(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)

	# Good atomics are important on aarch64, they exist on ARMv8.1a or higher
	# Check some known common machines and tell compiler if present
	execute_process(COMMAND uname -a COMMAND tr -d '\n' OUTPUT_VARIABLE UNAME_A)
	if(
		UNAME_A MATCHES rk3588 		# Orange Pi 5 variants
		OR
		UNAME_A MATCHES rock-5b		# Raxda Rock 5B
		OR
		UNAME_A MATCHES rpi-2712	# Raspberry Pi 5
		)
		string(APPEND CMAKE_CXX_FLAGS " -march=armv8.2-a")
		string(APPEND CMAKE_C_FLAGS " -march=armv8.2-a")
	endif()
endif()

message( STATUS "Architecture: ${ARCHITECTURE}" )
message( STATUS "Compiler Arguments: ${CMAKE_CXX_FLAGS}" )
message( STATUS "Compiler Arguments (Release): ${CMAKE_CXX_FLAGS_RELEASE}" )
message( STATUS "Compiler Arguments (Debug): ${CMAKE_CXX_FLAGS_DEBUG}" )
message( STATUS "Build Configration: ${CMAKE_BUILD_TYPE}" )

# VST3 SDK needs these
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_definitions(/D_DEBUG)
else()
	add_definitions(/DRELEASE)
endif()

# we need C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(UNIX)
	set(CMAKE_POSITION_INDEPENDENT_CODE ON)
	set(CMAKE_CXX_VISIBILITY_PRESET hidden)
	set(CMAKE_C_VISIBILITY_PRESET hidden)
	set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

set(PA_DISABLE_INSTALL ON)
set(PA_BUILD_SHARED OFF)
