find_package(Python3 REQUIRED COMPONENTS Interpreter)

# The catalog generator needs Python 3.12+ (f-strings with backslashes). The
# lookup above can still resolve the interpreter that ships with macOS (3.9),
# and stale cache entries are never refreshed, so force a modern interpreter
# when one is available on the build machine.
if(NOT Python3_VERSION VERSION_GREATER_EQUAL 3.12)
    find_program(_VITA3K_MODERN_PYTHON3 NAMES python3.14 python3.13 python3.12 NO_CMAKE_FIND_ROOT_PATH)
    if(_VITA3K_MODERN_PYTHON3)
        set(Python3_EXECUTABLE "${_VITA3K_MODERN_PYTHON3}" CACHE FILEPATH "Host Python 3 interpreter for build-time code generation" FORCE)
        # find_package leaves a normal variable that would shadow the cache.
        unset(Python3_EXECUTABLE)
    elseif(IOS)
        message(FATAL_ERROR "The i18n catalog generator requires Python 3.12+; none found on this machine.")
    endif()
endif()

set(LANG_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(LANG_GENERATOR "${CMAKE_SOURCE_DIR}/tools/i18n/generate_lang_catalog.py")
set(LANG_STRINGS_DEF "${CMAKE_CURRENT_SOURCE_DIR}/include/lang/strings.def")
file(GLOB LANG_JSON_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/i18n/lang/overlay_*.json")

set(LANG_GENERATED_HEADER "${LANG_GENERATED_DIR}/generated_catalog.h")
set(LANG_GENERATED_SOURCE "${LANG_GENERATED_DIR}/generated_catalog.cpp")

add_custom_command(
    OUTPUT
        "${LANG_GENERATED_HEADER}"
        "${LANG_GENERATED_SOURCE}"
    COMMAND
        ${Python3_EXECUTABLE}
        "${LANG_GENERATOR}"
        --input-dir "${CMAKE_SOURCE_DIR}/i18n/lang"
        --strings-def "${LANG_STRINGS_DEF}"
        --output-header "${LANG_GENERATED_HEADER}"
        --output-source "${LANG_GENERATED_SOURCE}"
    DEPENDS
        "${LANG_GENERATOR}"
        "${LANG_STRINGS_DEF}"
        ${LANG_JSON_FILES}
    VERBATIM
)

add_library(
	lang
	STATIC
	include/lang/state.h
	include/lang/strings.def
	src/lang.cpp
	"${LANG_GENERATED_HEADER}"
	"${LANG_GENERATED_SOURCE}"
)

target_link_libraries(lang PRIVATE util)

target_include_directories(lang
	PUBLIC
		include
	PRIVATE
		"${LANG_GENERATED_DIR}"
)
