add_compile_options(-w)

include(${CMAKE_ROOT}/Modules/ExternalProject.cmake)

# Make option() in all external projects do nothing when a normal variable of the
# same name exists. The normal variable is not removed. The cache entry is not
# created or updated and is ignored if it exists.
set(CMAKE_POLICY_DEFAULT_CMP0077 "NEW")

function(check_submodules_present)
	file(READ "${CMAKE_SOURCE_DIR}/.gitmodules" gitmodules)
	string(REGEX MATCHALL "path *= *[^ \t\r\n]*" gitmodules ${gitmodules})

	foreach(module ${gitmodules})
		# Get module name
		string(REGEX REPLACE "path *= *" "" module ${module})

		# Stat the folder and get amount of entries
		file(GLOB RESULT "${CMAKE_SOURCE_DIR}/${module}/*")
		list(LENGTH RESULT RES_LEN)

		# If the module has no files, bring fatal error
		if(RES_LEN EQUAL 0)
			# directory is empty
			message(FATAL_ERROR "Submodule ${module} is empty. "
				"Please run: git submodule update --init --recursive")
		endif()
	endforeach()
endfunction()

check_submodules_present()

# Set folder for external projects (for project tree in IDE)
set(CMAKE_FOLDER externals)

option(BUILD_SHARED_LIBS "Build shared library" OFF)
option(CAPSTONE_ARCHITECTURE_DEFAULT "Whether architectures are enabled by default" OFF)
option(CAPSTONE_ARM_SUPPORT "ARM support" ON)
if(IOS)
	# Capstone forces a universal x86_64+arm64 build on Apple hosts; an iOS
	# build must stay single-architecture arm64.
	set(CAPSTONE_BUILD_MACOS_THIN ON)
endif()
add_subdirectory(capstone EXCLUDE_FROM_ALL)

add_library(dirent INTERFACE)

if(MSVC)
	target_include_directories(dirent INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/dirent/include")
endif()

add_library(printf INTERFACE)
target_include_directories(printf INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/printf")

set(FMT_INSTALL ON)
add_subdirectory(fmt)
set(fmt_DIR fmt)

set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "")
set(SPDLOG_NO_THREAD_ID ON CACHE BOOL "")
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "")
add_subdirectory(spdlog EXCLUDE_FROM_ALL)

add_subdirectory(substitute EXCLUDE_FROM_ALL)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/stb")

add_library(ddspp INTERFACE)
target_include_directories(ddspp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/ddspp")

add_library(cppcommon STATIC CppCommon/source/cpu.cpp CppCommon/source/environment.cpp)
target_include_directories(cppcommon PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/CppCommon")

add_library(glad STATIC "${CMAKE_CURRENT_SOURCE_DIR}/glad/src/glad.c")
target_include_directories(glad PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/glad/include")

add_library(googletest STATIC googletest/googletest/src/gtest_main.cc googletest/googletest/src/gtest-all.cc)
target_include_directories(googletest PUBLIC googletest/googletest/include)
target_include_directories(googletest PRIVATE googletest/googletest)
target_compile_definitions(googletest PUBLIC GTEST_HAS_PTHREAD=0)

add_library(concurrentqueue INTERFACE)
target_include_directories(concurrentqueue INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/concurrentqueue")

add_subdirectory(libfat16)

add_library(miniz STATIC miniz/miniz.c miniz/miniz.h)
target_include_directories(miniz PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/miniz")

if(IOS)
	# Statically absorb SDL into the core library: an iOS app bundle cannot ship
	# arbitrary dylibs, and dlopen of shared frameworks is restricted on iOS.
	set(SDL_SHARED OFF CACHE BOOL "" FORCE)
	set(SDL_STATIC ON CACHE BOOL "" FORCE)
	set(SDL_DEPS_SHARED OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(sdl)

# Cubeb is optional on iOS: the initial release uses SDL3 for audio (SDL3 has a
# native iOS audio driver). The audio factory conditions itself on CUBEB_AUDIO.
if(NOT IOS)
	# Cubeb setup, from https://github.com/RPCS3/rpcs3/blob/master/3rdparty/cubeb/CMakeLists.txt
	set(BUILD_SHARED_LIBS FALSE CACHE BOOL "Don't build shared libs")
	set(BUILD_TESTS FALSE CACHE BOOL "Don't build tests")
	set(BUILD_RUST_LIBS FALSE CACHE BOOL "Don't build rust libs")
	set(BUILD_TOOLS FALSE CACHE BOOL "Don't build tools")
	set(BUNDLE_SPEEX TRUE CACHE BOOL "Bundle the speex library")
	set(LAZY_LOAD_LIBS TRUE CACHE BOOL "Lazily load shared libraries")
	set(USE_SANITIZERS FALSE CACHE BOOL "Dont't use sanitizers")

	add_subdirectory(cubeb EXCLUDE_FROM_ALL)

	if (ARCHITECTURE STREQUAL "arm64")
		target_compile_definitions(speex PUBLIC
			#_USE_NEON
		)
	elseif (ARCHITECTURE STREQUAL "x86_64")
		target_compile_definitions(speex PUBLIC
			_USE_SSE
			_USE_SSE2
		)
	endif ()
endif()

if(ANDROID)
	if(ARCHITECTURE STREQUAL "arm64")
		add_subdirectory(libadrenotools)
		target_compile_definitions(adrenotools INTERFACE USE_ADRENO_TOOLS)
	else()
		add_library(adrenotools INTERFACE)
	endif()
endif()

if(WIN32)
	add_library(winsock INTERFACE)
	find_library(WSOCK32 wsock32)
	find_library(WS2_32 ws2_32)
	find_library(IPHLPAPI iphlpapi)
	target_link_libraries(winsock INTERFACE WSOCK32 WS2_32 IPHLPAPI)
endif()

set(DYNARMIC_TESTS OFF CACHE BOOL "")
set(DYNARMIC_NO_BUNDLED_FMT ON CACHE BOOL "")
set(DYNARMIC_FRONTENDS "A32" CACHE STRING "")
add_subdirectory(dynarmic)

	if(IOS)
		# spin_lock_arm64.cpp anchors its emitted lock/unlock code in the
		# process-wide JIT arena on physical iPhones (TXM rejects plain
		# executable mmaps and mprotect-to-RW fails silently there).
		target_include_directories(dynarmic PRIVATE ${CMAKE_SOURCE_DIR}/vita3k/ios/include)
	endif()

if(MSVC)
	if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
		get_target_property(MCL_COMPILE_OPTIONS mcl COMPILE_OPTIONS)
		list(REMOVE_ITEM MCL_COMPILE_OPTIONS /std:c++latest)
		set_property(TARGET mcl PROPERTY COMPILE_OPTIONS ${MCL_COMPILE_OPTIONS})
	endif()
endif()

add_library(vita-toolchain INTERFACE)
target_include_directories(vita-toolchain INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/vita-toolchain/src")

option(YAML_CPP_BUILD_TOOLS "Enable parse tools" OFF)
option(YAML_CPP_BUILD_CONTRIB "Enable contrib stuff in library" OFF)
add_subdirectory(yaml-cpp EXCLUDE_FROM_ALL)

if(USE_DISCORD_RICH_PRESENCE)
	if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/discord_game_sdk.zip")
		message(STATUS "Downloading discord gamesdk...")
		file(DOWNLOAD https://dl-game-sdk.discordapp.net/latest/discord_game_sdk.zip
			"${CMAKE_BINARY_DIR}/external/discord_game_sdk.zip"
			SHOW_PROGRESS
			EXPECTED_HASH SHA256=6757bb4a1f5b42aa7b6707cbf2158420278760ac5d80d40ca708bb01d20ae6b4)
	endif()

	if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/discord_game_sdk")
		file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/external/discord_game_sdk")
		execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${CMAKE_BINARY_DIR}/external/discord_game_sdk.zip"
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/discord_game_sdk")
		file(RENAME "${CMAKE_BINARY_DIR}/external/discord_game_sdk/lib/x86_64/discord_game_sdk.so"
			"${CMAKE_BINARY_DIR}/external/discord_game_sdk/lib/x86_64/libdiscord_game_sdk.so")
	endif()

	add_library(discord-rpc STATIC
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/achievement_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/activity_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/application_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/core.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/image_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/lobby_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/network_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/overlay_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/relationship_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/storage_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/store_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/types.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/user_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/voice_manager.cpp)

	if(APPLE)
		if(ARCHITECTURE STREQUAL "x86_64")
			target_link_libraries(discord-rpc PUBLIC "${CMAKE_BINARY_DIR}/external/discord_game_sdk/lib/x86_64/discord_game_sdk.dylib")
		elseif(ARCHITECTURE STREQUAL "arm64")
			target_link_libraries(discord-rpc PUBLIC "${CMAKE_BINARY_DIR}/external/discord_game_sdk/lib/aarch64/discord_game_sdk.dylib")
		else()
			execute_process(COMMAND lipo -create -output discord_game_sdk.dylib x86_64/discord_game_sdk.dylib aarch64/discord_game_sdk.dylib
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/discord_game_sdk/lib")
			target_link_libraries(discord-rpc PUBLIC "${CMAKE_BINARY_DIR}/external/discord_game_sdk/lib/discord_game_sdk.dylib")
		endif()
	elseif(WIN32)
		target_link_libraries(discord-rpc PUBLIC "${CMAKE_BINARY_DIR}/external/discord_game_sdk/lib/x86_64/discord_game_sdk.dll.lib")
	elseif(UNIX)
		target_link_libraries(discord-rpc PUBLIC "${CMAKE_BINARY_DIR}/external/discord_game_sdk/lib/x86_64/libdiscord_game_sdk.so")
	endif()

	target_include_directories(discord-rpc PUBLIC "${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp")
endif()

option(BUILD_EXTERNAL "Build external dependencies in /External" OFF)
option(ENABLE_SPVREMAPPER "Enables building of SPVRemapper" OFF)
option(ENABLE_GLSLANG_BINARIES "Builds glslang and spirv-remap" OFF)
option(ENABLE_HLSL "Enables HLSL input support" OFF)
option(GLSLANG_TESTS "Enable glslang testing" OFF)
option(ENABLE_OPT "Enables spirv-opt capability if present" OFF)
add_subdirectory(glslang)

option(SPIRV_CROSS_CLI "Build the CLI binary. Requires SPIRV_CROSS_STATIC." OFF)
option(SPIRV_CROSS_ENABLE_TESTS "Enable SPIRV-Cross tests." OFF)
option(SPIRV_CROSS_ENABLE_HLSL "Enable HLSL target support." OFF)
option(SPIRV_CROSS_ENABLE_MSL "Enable MSL target support." OFF)
option(SPIRV_CROSS_ENABLE_CPP "Enable C++ target support." OFF)
option(SPIRV_CROSS_ENABLE_REFLECT "Enable JSON reflection target support." OFF)
option(SPIRV_CROSS_ENABLE_C_API "Enable C API wrapper support in static library." OFF)
option(SPIRV_CROSS_ENABLE_UTIL "Enable util module support." OFF)
option(SPIRV_CROSS_SKIP_INSTALL "Skips installation targets." ON)
add_subdirectory(SPIRV-Cross)

add_library(dlmalloc STATIC "${CMAKE_CURRENT_SOURCE_DIR}/dlmalloc/dlmalloc.cc")
target_include_directories(dlmalloc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/dlmalloc")
target_compile_definitions(dlmalloc PUBLIC ONLY_MSPACES=1)
target_compile_definitions(dlmalloc PUBLIC USE_LOCK=0)

add_subdirectory(pugixml)

add_library(CLI11 INTERFACE)
target_include_directories(CLI11 INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/cli11")

add_library(vulkan INTERFACE)
target_include_directories(vulkan INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/VulkanMemoryAllocator-Hpp/Vulkan-Headers/include")
if(APPLE AND NOT IOS)
	if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/MoltenVK-macos.tar")
		message(STATUS "Downloading MoltenVK...")
		file(DOWNLOAD https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.1/MoltenVK-macos.tar
			"${CMAKE_BINARY_DIR}/external/MoltenVK-macos.tar"
			SHOW_PROGRESS
			EXPECTED_HASH SHA256=5ea0c259df7ded9a275444820f09cced54d6e5a7c7a31d262de62a5cdb7e15cf)
	endif()
	if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/MoltenVK")
		execute_process(COMMAND tar xf "${CMAKE_BINARY_DIR}/external/MoltenVK-macos.tar" --strip-components=1 MoltenVK/MoltenVK 
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external")
	endif()
	target_include_directories(vulkan INTERFACE "${CMAKE_BINARY_DIR}/external/MoltenVK/include")
	set(MOLTENVK_DYLIB "${CMAKE_BINARY_DIR}/external/MoltenVK/dylib/macOS/libMoltenVK.dylib" PARENT_SCOPE)
elseif(IOS)
	# MoltenVK is shipped for iOS as an XCFramework (static lib) plus headers.
	# The core only needs the headers: the Vulkan API symbols are resolved at
	# runtime from the XCFramework that the app links. v1.4.1 ships a single
	# `ios-arm64` (device) slice, so the simulator app cannot present Vulkan
	# until a newer MoltenVK (or a source build) provides that slice.
	if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/MoltenVK-ios.tar")
		message(STATUS "Downloading MoltenVK (iOS)...")
		file(DOWNLOAD https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.1/MoltenVK-ios.tar
			"${CMAKE_BINARY_DIR}/external/MoltenVK-ios.tar"
			SHOW_PROGRESS
			EXPECTED_HASH SHA256=54336b90212c390ed5935c96460aed3bf651ad7d3c0f0e956586ce18e9c0b701)
	endif()
	if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/MoltenVK/include")
		execute_process(COMMAND tar xf "${CMAKE_BINARY_DIR}/external/MoltenVK-ios.tar" --strip-components=1 MoltenVK/MoltenVK
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external")
	endif()
	target_include_directories(vulkan INTERFACE "${CMAKE_BINARY_DIR}/external/MoltenVK/include")
	set(MOLTENVK_IOS_XCFRAMEWORK "${CMAKE_BINARY_DIR}/external/MoltenVK/static/MoltenVK.xcframework" PARENT_SCOPE)
elseif(ANDROID AND NOT (CMAKE_BUILD_TYPE STREQUAL "Release"))
	# add validation layer
	if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../android/prebuilt/${ANDROID_ABI}/libVkLayer_khronos_validation.so")
		set(VALIDATION_BINARY "android-binaries-1.4.341.0")

		# download it if not already done
		if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/validation_layers.zip")
			message(STATUS "Downloading validation layer...")
			file(DOWNLOAD https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases/download/vulkan-sdk-1.4.341.0/${VALIDATION_BINARY}.zip
				"${CMAKE_BINARY_DIR}/external/validation_layers.zip"
				SHOW_PROGRESS
				EXPECTED_HASH SHA256=b47aa5c4a65d9787ef051276587f50cfd70dcd822a8c1b7d4d4a92e28e993459)
		endif()

		if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/validation_layers")
			file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/external/validation_layers")
			execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${CMAKE_BINARY_DIR}/external/validation_layers.zip"
				WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/validation_layers")
		endif()

		file(COPY "${CMAKE_BINARY_DIR}/external/validation_layers/${VALIDATION_BINARY}/${ANDROID_ABI}" DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/../android/prebuilt")
	endif()
endif()

add_library(vma INTERFACE)
target_include_directories(vma INTERFACE
	"${CMAKE_CURRENT_SOURCE_DIR}/VulkanMemoryAllocator-Hpp/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/VulkanMemoryAllocator-Hpp/VulkanMemoryAllocator/include")

if(ANDROID)
	set(USE_STATIC_ANDROID_FFMPEG ON)
endif()
add_subdirectory(ffmpeg)

if(IOS)
	# The FFmpeg fork's APPLE branch links macOS-only frameworks (AudioUnit,
	# VideoToolbox). The iOS prebuilt only needs the portable set, so strip the
	# Apple frameworks it adds and re-add the ones the iOS build actually links.
	get_target_property(_ffmpeg_link_libs ffmpeg INTERFACE_LINK_LIBRARIES)
	foreach(_ff_lib IN LISTS _ffmpeg_link_libs)
		if(_ff_lib MATCHES "^-framework (AudioUnit|VideoToolbox)$")
			list(REMOVE_ITEM _ffmpeg_link_libs "${_ff_lib}")
		endif()
	endforeach()
	set_property(TARGET ffmpeg PROPERTY INTERFACE_LINK_LIBRARIES "${_ffmpeg_link_libs}")

	# The fork selects the prebuilt with a case-sensitive match on the sysroot
	# name; after project() the variable holds the resolved SDK path
	# ("...iPhoneSimulator.sdk"), which does not match "iphonesimulator", so the
	# DEVICE prebuilt ends up downloaded for simulator builds. Force the
	# simulator prebuilt (same release, keyed by the pinned submodule SHA).
	if(CMAKE_OSX_SYSROOT MATCHES "[iI]Phone[sS]imulator")
		# Source archives may lack submodule metadata. Allow an explicit pinned
		# revision rather than attempting a download with an empty release tag.
		set(VITA3K_FFMPEG_SHA "" CACHE STRING "FFmpeg prebuilt release tag (auto-detected from the submodule when empty)")
		if(NOT VITA3K_FFMPEG_SHA)
			execute_process(
				COMMAND git rev-parse --short HEAD
				WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg"
				OUTPUT_VARIABLE VITA3K_FFMPEG_SHA
				OUTPUT_STRIP_TRAILING_WHITESPACE)
		endif()
		if(NOT VITA3K_FFMPEG_SHA)
			message(FATAL_ERROR "FFmpeg submodule revision unavailable; restore its Git metadata or set VITA3K_FFMPEG_SHA to the pinned prebuilt release tag")
		endif()
		if(ARCHITECTURE STREQUAL "x86_64")
			set(_ffmpeg_sim_zip "ffmpeg-ios-simulator-x64.zip")
		else()
			set(_ffmpeg_sim_zip "ffmpeg-ios-simulator-${ARCHITECTURE}.zip")
		endif()
		set(_ffmpeg_fixup_stamp "${CMAKE_BINARY_DIR}/external/ffmpeg-ios-simulator.stamp")
		if(EXISTS "${_ffmpeg_fixup_stamp}")
			file(READ "${_ffmpeg_fixup_stamp}" _ffmpeg_stamp)
		else()
			set(_ffmpeg_stamp "")
		endif()
		if(NOT _ffmpeg_stamp STREQUAL "${VITA3K_FFMPEG_SHA}")
			message(STATUS "FFmpeg simulator fixup: downloading ${_ffmpeg_sim_zip} (commit ${VITA3K_FFMPEG_SHA})...")
			file(DOWNLOAD https://github.com/Vita3K/ffmpeg-core/releases/download/${VITA3K_FFMPEG_SHA}/${_ffmpeg_sim_zip}
				"${CMAKE_BINARY_DIR}/external/ffmpeg-ios-simulator.zip"
				SHOW_PROGRESS
				STATUS _ffmpeg_download_status)
			list(GET _ffmpeg_download_status 0 _ffmpeg_download_code)
			if(NOT _ffmpeg_download_code EQUAL 0)
				file(REMOVE "${CMAKE_BINARY_DIR}/external/ffmpeg-ios-simulator.zip")
				message(FATAL_ERROR "No FFmpeg simulator prebuilt found with commit SHA (${VITA3K_FFMPEG_SHA})")
			endif()
			file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/external/ffmpeg/lib")
			file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/external/ffmpeg/lib")
			execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${CMAKE_BINARY_DIR}/external/ffmpeg-ios-simulator.zip"
				WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/ffmpeg/lib")
			file(WRITE "${_ffmpeg_fixup_stamp}" "${VITA3K_FFMPEG_SHA}")
		endif()
	endif()
endif()

if(IOS)
	# Cross-build a static OpenSSL for the iOS target. The host (macOS/Homebrew)
	# copy can never be linked into an iOS binary.
	set(OPENSSL_IOS_ROOT "${CMAKE_BINARY_DIR}/external/openssl-ios")
	if(NOT EXISTS "${OPENSSL_IOS_ROOT}/libssl.a")
		if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/openssl-ios.tar.gz")
			message(STATUS "Downloading OpenSSL@4 source...")
			file(DOWNLOAD https://github.com/openssl/openssl/releases/download/openssl-4.0.1/openssl-4.0.1.tar.gz
				"${CMAKE_BINARY_DIR}/external/openssl-ios.tar.gz"
				SHOW_PROGRESS
				EXPECTED_HASH SHA256=2db3f3a0d6ea4b59e1f094ace2c8cd536dffb87cdc39084c5afa1e6f7f37dd09)
		endif()

		file(MAKE_DIRECTORY "${OPENSSL_IOS_ROOT}")
		execute_process(COMMAND tar xzf "${CMAKE_BINARY_DIR}/external/openssl-ios.tar.gz"
			-C "${OPENSSL_IOS_ROOT}" --strip-components=1
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external")

		if(CMAKE_OSX_SYSROOT MATCHES "[iI]Phone[sS]imulator")
			set(IOS_OPENSSL_TARGET "iossimulator-arm64-xcrun")
			set(IOS_OPENSSL_VERSION_FLAG "-miphonesimulator-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
		else()
			set(IOS_OPENSSL_TARGET "ios64-xcrun")
			set(IOS_OPENSSL_VERSION_FLAG "-miphoneos-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
		endif()

		# The `*-xcrun` targets pick the SDK compiler via `xcrun` and stamp the
		# objects with platform IOS (never the macOS deployment flag, which
		# would retarget the build to the macOS platform).
		set(IOS_OPENSSL_PREFIX "--prefix=${OPENSSL_IOS_ROOT}")
		set(IOS_OPENSSL_OPENSSLSDIR "--openssldir=${OPENSSL_IOS_ROOT}")
		execute_process(
			# CMake exports the host CC/CXX; let OpenSSL's xcrun target select
			# the iOS compiler and SDK instead of inheriting the macOS SDK.
			COMMAND ${CMAKE_COMMAND} -E env --unset=CC --unset=CXX
				"SDKROOT=${CMAKE_OSX_SYSROOT}" perl ./Configure ${IOS_OPENSSL_TARGET}
				${IOS_OPENSSL_PREFIX}
				${IOS_OPENSSL_OPENSSLSDIR}
				${IOS_OPENSSL_VERSION_FLAG}
				no-tests
				no-docs
				no-shared
			WORKING_DIRECTORY "${OPENSSL_IOS_ROOT}"
			RESULT_VARIABLE OPENSSL_CONFIGURE_RESULT)
		if(NOT OPENSSL_CONFIGURE_RESULT EQUAL 0)
			message(FATAL_ERROR "OpenSSL 'Configure ${IOS_OPENSSL_TARGET}' failed")
		endif()

		message(STATUS "Building static OpenSSL for ${IOS_OPENSSL_TARGET}...")
		# build_libs skips the openssl CLI app and the dylibs: iOS has no
		# syslog (bss_log) and we only consume the static archives.
		execute_process(COMMAND make build_libs -j${CPU_COUNT}
			WORKING_DIRECTORY "${OPENSSL_IOS_ROOT}"
			RESULT_VARIABLE OPENSSL_MAKE_RESULT)
		if(NOT OPENSSL_MAKE_RESULT EQUAL 0)
			message(FATAL_ERROR "OpenSSL 'make build_libs' failed for target ${IOS_OPENSSL_TARGET}")
		endif()
	endif()

	set(OPENSSL_ROOT_DIR "${OPENSSL_IOS_ROOT}")
	set(OPENSSL_USE_STATIC_LIBS TRUE)
	# `make build_libs` drops the archives and headers in the tree root; FindOpenSSL
	# only looks under <root>/lib, so pre-set the result variables.
	set(OPENSSL_INCLUDE_DIR "${OPENSSL_IOS_ROOT}/include" CACHE PATH "" FORCE)
	set(OPENSSL_ssl_LIBRARY "${OPENSSL_IOS_ROOT}/libssl.a" CACHE FILEPATH "" FORCE)
	set(OPENSSL_crypto_LIBRARY "${OPENSSL_IOS_ROOT}/libcrypto.a" CACHE FILEPATH "" FORCE)
	set(OPENSSL_SSL_LIBRARY "${OPENSSL_IOS_ROOT}/libssl.a" CACHE FILEPATH "" FORCE)
	set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_IOS_ROOT}/libcrypto.a" CACHE FILEPATH "" FORCE)
	find_package(OpenSSL QUIET)
	if(NOT OPENSSL_FOUND)
		message(FATAL_ERROR "Cross-built OpenSSL not found at ${OPENSSL_IOS_ROOT}; delete the directory and re-run configure.")
	endif()
elseif(APPLE OR NOT FORCE_BUILD_OPENSSL_MAC)
	execute_process(
		COMMAND brew --prefix openssl
		RESULT_VARIABLE BREW_RESULT
		OUTPUT_VARIABLE BREW_OPENSSL
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	if(BREW_RESULT EQUAL 0)
		set(OPENSSL_ROOT_DIR "${BREW_OPENSSL}")
		set(OPENSSL_USE_STATIC_LIBS TRUE)
	endif()
endif()

if(NOT FORCE_BUILD_OPENSSL_MAC)
	find_package(OpenSSL QUIET)
endif()

if(NOT IOS AND (NOT OPENSSL_FOUND OR FORCE_BUILD_OPENSSL_MAC))
	if(MSVC)
		message("OpenSSL not found, using prebuilt version")

		if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/openssl.zip")
			message(STATUS "Downloading openssl...")
			file(DOWNLOAD https://download.firedaemon.com/FireDaemon-OpenSSL/openssl-4.0.1.zip
				"${CMAKE_BINARY_DIR}/external/openssl.zip"
				SHOW_PROGRESS
				EXPECTED_HASH SHA256=b83e202841fc163f9fe18bbbd1e90e69117bf37cc41e1116701729c0c3bcd0b3)
		endif()

		if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/openssl")
			file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/external/openssl")
			execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${CMAKE_BINARY_DIR}/external/openssl.zip"
				WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/openssl")
		endif()

		set(OPENSSL_ROOT_DIR "${CMAKE_BINARY_DIR}/external/openssl/x64" CACHE STRING "Path to OpenSSL root")
	elseif(APPLE)
		message(STATUS "OpenSSL not found.")
		if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/openssl.tar.gz")
			message(STATUS "Downloading OpenSSL@4 from source...")
			file(DOWNLOAD https://github.com/openssl/openssl/releases/download/openssl-4.0.1/openssl-4.0.1.tar.gz
				"${CMAKE_BINARY_DIR}/external/openssl.tar.gz"
				SHOW_PROGRESS
				EXPECTED_HASH SHA256=2db3f3a0d6ea4b59e1f094ace2c8cd536dffb87cdc39084c5afa1e6f7f37dd09)
		endif()

		if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/openssl")
			file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/external/openssl")
			execute_process(COMMAND tar xfp "${CMAKE_BINARY_DIR}/external/openssl.tar.gz" -C openssl --strip-components=1
				WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external")
		endif()

		if (NOT EXISTS "${CMAKE_BINARY_DIR}/external/openssl/libssl.a" OR NOT EXISTS "${CMAKE_BINARY_DIR}/external/openssl/libcrypto.a")
			message(STATUS "Building OpenSSL@4...")
			if (ARCHITECTURE STREQUAL "x86_64" OR ARCHITECTURE STREQUAL "arm64")
				execute_process(COMMAND perl Configure darwin64-${ARCHITECTURE} -mmacosx-version-min=11 WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/openssl")
				execute_process(COMMAND make -j${CPU_COUNT} WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/openssl" OUTPUT_QUIET)
			else()
				if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/openssl/arm64")
					file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/external/openssl/arm64")
					execute_process(COMMAND perl ../Configure darwin64-arm64 -mmacosx-version-min=11 WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/openssl/arm64")
					execute_process(COMMAND make -j${CPU_COUNT} WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/openssl/arm64" OUTPUT_QUIET)	
				endif()
				if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/openssl/x86_64")
					file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/external/openssl/x86_64")
					execute_process(COMMAND perl ../Configure darwin64-x86_64 -mmacosx-version-min=11 WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/openssl/x86_64")
					execute_process(COMMAND make -j${CPU_COUNT} WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/openssl/x86_64" OUTPUT_QUIET)
				endif()
				execute_process(COMMAND lipo -create arm64/libssl.a x86_64/libssl.a -output libssl.a WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/openssl")
				execute_process(COMMAND lipo -create arm64/libcrypto.a x86_64/libcrypto.a -output libcrypto.a WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/openssl")
			endif()
		endif()

		set(OPENSSL_ROOT_DIR "${CMAKE_BINARY_DIR}/external/openssl")
		set(OPENSSL_USE_STATIC_LIBS TRUE)
	endif()
	find_package(OpenSSL REQUIRED)
endif()

message("OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
message("OpenSSL libraries: ${OPENSSL_LIBRARIES}")

add_library(ssl INTERFACE)
target_link_libraries(ssl INTERFACE OpenSSL::SSL)
add_library(crypto INTERFACE)
target_link_libraries(crypto INTERFACE OpenSSL::Crypto)

# Must be after openssl
add_subdirectory(psvpfstools)

if(IOS)
	# The FetchContent sub-build of libcurl only inherits cache variables: pin
	# the cross-built OpenSSL there so it is never resolved from the host.
	set(OPENSSL_ROOT_DIR "${OPENSSL_ROOT_DIR}" CACHE PATH "" FORCE)
	set(OPENSSL_USE_STATIC_LIBS TRUE CACHE BOOL "" FORCE)
	set(OPENSSL_INCLUDE_DIR "${OPENSSL_INCLUDE_DIR}" CACHE PATH "" FORCE)
	set(OPENSSL_ssl_LIBRARY "${OPENSSL_ROOT_DIR}/libssl.a" CACHE FILEPATH "" FORCE)
	set(OPENSSL_crypto_LIBRARY "${OPENSSL_ROOT_DIR}/libcrypto.a" CACHE FILEPATH "" FORCE)
endif()

find_package(CURL QUIET)
if(NOT CURL_FOUND)
	message("System curl not found, compiling curl from source")
	include(FetchContent)
	option(BUILD_CURL_EXE "Build curl executable" OFF)
	option(CURL_DISABLE_INSTALL "Disable installation targets" ON)
	option(HTTP_ONLY "Disable all protocols except HTTP (This overrides all CURL_DISABLE_* options)" ON)
	option(BUILD_LIBCURL_DOCS "Build libcurl man pages" OFF)
	option(BUILD_MISC_DOCS "Build misc man pages (e.g. curl-config and mk-ca-bundle)" OFF)
	option(ENABLE_CURL_MANUAL "Build the man page for curl and enable its -M/--manual option" OFF)
	if(WIN32)
		option(CURL_USE_SCHANNEL "Enable Windows native SSL/TLS (Schannel)" ON)
	endif()
	option(CURL_CA_FALLBACK "Use built-in CA store of OpenSSL. Defaults to OFF" ON)
	option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
	option(BUILD_STATIC_LIBS "Build static libraries" ON)
	option(BUILD_TESTING "Build tests" OFF)
	option(BUILD_EXAMPLES "Build libcurl examples" OFF)
	option(CURL_USE_LIBPSL "Use libpsl" OFF)
	FetchContent_Declare(curl
		GIT_REPOSITORY https://github.com/curl/curl.git
		GIT_TAG        68720b4837284335b2d63cb358f8f6ce65f5bc55 # release 8.21.0
	)
	FetchContent_MakeAvailable(curl)
else()
	add_library(libcurl INTERFACE)
	target_link_libraries(libcurl INTERFACE CURL::libcurl)
endif()


file(GLOB LIBATRAC9_SOURCES
	LibAtrac9/C/src/*.c
	LibAtrac9/C/src/*.h
)
add_library(libatrac9 STATIC ${LIBATRAC9_SOURCES})

target_include_directories(libatrac9 PUBLIC LibAtrac9/C/src)

option(BUILD_SHARED_LIBS "Build shared library" OFF)
option(XXHASH_BUILD_XXHSUM "Build the xxhsum binary" OFF)
option(XXH_X86DISPATCH_ALLOW_AVX "Allow building XXH3 with AVX even if it crashes on SSE2-Only CPUs" OFF)
if((NOT APPLE) AND (ARCHITECTURE STREQUAL "x86_64"))
	set(DISPATCH 1)
endif()
add_subdirectory(xxHash/cmake_unofficial EXCLUDE_FROM_ALL)
if(XXH_X86DISPATCH_ALLOW_AVX)
	target_compile_definitions(xxhash PRIVATE XXH_X86DISPATCH_ALLOW_AVX)
endif()

# Tracy
# The target always exists so that `target_link_libraries(x tracy)` never degrades
# into a bare `-ltracy` flag (which breaks single-config Release reconfigures).
# In Release, TRACY_ENABLE stays undefined and the client compiles to a no-op.
add_library(tracy STATIC tracy/public/TracyClient.cpp)
target_include_directories(tracy SYSTEM PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tracy/public>)
target_link_libraries(tracy PUBLIC ${CMAKE_DL_LIBS})

if(UNIX AND NOT ANDROID)
	target_link_libraries(tracy PUBLIC pthread)
endif()

if(NOT (CMAKE_BUILD_TYPE STREQUAL "Release"))
	option(TRACY_ENABLE_ON_CORE_COMPONENTS
		"Enable and require Tracy to compile core components such as the renderer, shader recompiler and
			HLE modules"
		ON)
	option(TRACY_NO_FRAME_IMAGE, ON)

	# Enable Tracy on-demand profiling mode to avoid unnecessary memory usage when the developer
	# isn't profiling by only enabling profiling after a connection has been established with
	# a Tracy server
	target_compile_definitions(tracy PUBLIC $<$<CONFIG:Debug,RelWithDebInfo>:TRACY_ON_DEMAND>)

	# Disable Tracy automated data collection in order to prevent Tracy-related code from being profiled
	target_compile_definitions(tracy PUBLIC TRACY_NO_SYSTEM_TRACING)

	# Disable Tracy crash handler. Don't stop tracing on first AV.
	target_compile_definitions(tracy PUBLIC TRACY_NO_CRASH_HANDLER)

	# Defining TRACY_ENABLE for both compiling Tracy and compiling the project that links
	# against it is needed for Tracy to work
	#
	# -------------------------- Options (choose one at a time) ---------------------------
	#
	# A (DEFAULT). Enable Tracy on any target that links against if the build configuration
	# is either Debug and RelWithDebInfo
	#
	# Using generator expresions instead of a conditional with CMAKE_BUILD_TYPE is needed
	# in order for the condition to work properly on both single-config and multi-config
	# CMake project generators. More info here:
	# https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html#build-configurations
	target_compile_definitions(tracy PUBLIC $<$<CONFIG:Debug,RelWithDebInfo>:TRACY_ENABLE>)
endif()

#
# ----------------------------------------------------------------------------------------
#
# B. Enable Tracy on every target that links to it regardless of the
# build configuration:
#
# target_compile_definitions(tracy PUBLIC TRACY_ENABLE)

# Use XDG desktop portals on Linux
set(NFD_PORTAL ON)

if(NOT ANDROID AND NOT IOS)
	# nativefiledialog-extended (desktop dialog boxes; the iOS app uses the
	# document picker instead)
	add_subdirectory(nativefiledialog-extended)

	# Create alias to prevent the need of multiple changes in case the target name changes
	add_library(NFDe::NFDe ALIAS nfd)
endif()
