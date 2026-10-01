if (NOT DEFINED FFMPEG_CORE_NAME)
	set(FFMPEG_CORE_NAME ffmpeg)
endif()

add_library(${FFMPEG_CORE_NAME} INTERFACE)

target_include_directories(${FFMPEG_CORE_NAME} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Detect OS
if (WIN32 AND NOT MINGW)
	set(FFMPEG_PREBUILTS_NAME "ffmpeg-windows")
elseif (IOS AND (CMAKE_OSX_SYSROOT MATCHES "iphonesimulator" OR ARCHITECTURE STREQUAL "x86_64"))
	set(FFMPEG_PREBUILTS_NAME "ffmpeg-ios-simulator")
elseif (IOS)
	set(FFMPEG_PREBUILTS_NAME "ffmpeg-ios")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	set(FFMPEG_PREBUILTS_NAME "ffmpeg-macos")
elseif (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
	set(FFMPEG_PREBUILTS_NAME "ffmpeg-freebsd")
elseif (ANDROID)
	set(FFMPEG_PREBUILTS_NAME "ffmpeg-android")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(FFMPEG_PREBUILTS_NAME "ffmpeg-linux")
elseif (MINGW)
	set(FFMPEG_PREBUILTS_NAME "ffmpeg-mingw")
else ()
	message(FATAL_ERROR "Unsupported OS.")
endif ()

# Detect Architecture
if(NOT DEFINED ARCHITECTURE)
	message(FATAL_ERROR "ARCHITECTURE variable is not set up")
elseif (ARCHITECTURE STREQUAL "x86_64")
	set(FFMPEG_PREBUILTS_NAME "${FFMPEG_PREBUILTS_NAME}-x64.zip")
elseif(ARCHITECTURE STREQUAL "arm")
	set(FFMPEG_PREBUILTS_NAME "${FFMPEG_PREBUILTS_NAME}-arm.zip")
elseif(ARCHITECTURE STREQUAL "arm64")
	set(FFMPEG_PREBUILTS_NAME "${FFMPEG_PREBUILTS_NAME}-arm64.zip")
elseif(APPLE AND "x86_64" IN_LIST ARCHITECTURE AND "arm64" IN_LIST ARCHITECTURE) # macOS universal
	set(FFMPEG_PREBUILTS_NAME "${FFMPEG_PREBUILTS_NAME}-universal.zip")
else ()
	message(FATAL_ERROR "Unsupported architecture.")
endif()

# Source snapshots in this fork include FFmpeg without its submodule metadata.
# Never use the parent repository's HEAD as an FFmpeg release tag after --clean.
set(VITA3K_FFMPEG_SHA "" CACHE STRING "FFmpeg prebuilt release tag (empty = pinned source revision)")
if(VITA3K_FFMPEG_SHA)
	set(FFMPEG_GIT_SHA "${VITA3K_FFMPEG_SHA}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
	execute_process(
		COMMAND git rev-parse --short=7 HEAD
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE FFMPEG_GIT_SHA
		OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE FFMPEG_GIT_RESULT
		ERROR_QUIET)
	if(NOT FFMPEG_GIT_RESULT EQUAL 0)
		set(FFMPEG_GIT_SHA "")
	endif()
endif()
if(NOT FFMPEG_GIT_SHA)
	# e74419374a148684911404792379cac284f3f603; see ios-plan.md.
	set(FFMPEG_GIT_SHA "e744193")
endif()

# Download prebuilt ffmpeg
if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/ffmpeg.zip")
message(STATUS "Downloading FFMPEG prebuilt ${FFMPEG_PREBUILTS_NAME} for commit ${FFMPEG_GIT_SHA}...")
	file(DOWNLOAD https://github.com/Vita3K/ffmpeg-core/releases/download/${FFMPEG_GIT_SHA}/${FFMPEG_PREBUILTS_NAME}
		"${CMAKE_BINARY_DIR}/external/ffmpeg.zip" SHOW_PROGRESS
		STATUS FILE_STATUS)
	list(GET FILE_STATUS 0 STATUS_CODE)
	if (NOT STATUS_CODE EQUAL 0)
		file(REMOVE "${CMAKE_BINARY_DIR}/external/ffmpeg.zip") # CMake create 0 byte file even if URL is invalid. So need to delete it.
		message(FATAL_ERROR "No FFMPEG prebuilt found with corresponding commit SHA (${FFMPEG_GIT_SHA})")
	endif()
endif()

if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/ffmpeg/lib")
	file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/external/ffmpeg/lib")
	execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${CMAKE_BINARY_DIR}/external/ffmpeg.zip"
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/ffmpeg/lib")
endif()

set(LIB_PREFIX "lib")
set(LIB_EXT "a")
if (WIN32 AND NOT MINGW)
	set(LIB_PREFIX "")
	set(LIB_EXT "lib")
	target_link_libraries(${FFMPEG_CORE_NAME} INTERFACE
		"psapi;strmiids;uuid;oleaut32;shlwapi;ws2_32;ole32;user32;bcrypt")
elseif (APPLE)
	target_link_libraries(${FFMPEG_CORE_NAME} INTERFACE
		"-framework CoreServices" "-framework CoreFoundation" "-framework AudioUnit"
		"-framework AudioToolbox" "-framework CoreAudio" "-framework CoreMedia"
		"-framework VideoToolbox" "-framework CoreVideo" "-framework Security")
endif ()

target_link_libraries(${FFMPEG_CORE_NAME} INTERFACE
	"${CMAKE_BINARY_DIR}/external/ffmpeg/lib/${LIB_PREFIX}avformat.${LIB_EXT}"
	"${CMAKE_BINARY_DIR}/external/ffmpeg/lib/${LIB_PREFIX}avcodec.${LIB_EXT}"
	"${CMAKE_BINARY_DIR}/external/ffmpeg/lib/${LIB_PREFIX}swscale.${LIB_EXT}"
	"${CMAKE_BINARY_DIR}/external/ffmpeg/lib/${LIB_PREFIX}avutil.${LIB_EXT}"
	"${CMAKE_BINARY_DIR}/external/ffmpeg/lib/${LIB_PREFIX}avfilter.${LIB_EXT}"
	"${CMAKE_BINARY_DIR}/external/ffmpeg/lib/${LIB_PREFIX}swresample.${LIB_EXT}")
