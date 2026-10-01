add_library(
	renderer
	STATIC
	src/gl/attribute_formats.cpp
	src/gl/color_formats.cpp
	src/gl/compile_program.cpp
	src/gl/draw.cpp
	src/gl/fence.cpp
	src/gl/renderer.cpp
	src/gl/ring_buffer.cpp
	src/gl/overlay_renderer.cpp
	src/gl/screen_render.cpp
	src/gl/surface_cache.cpp
	src/gl/sync_state.cpp
	src/gl/texture_formats.cpp
	src/gl/texture.cpp
	src/gl/uniforms.cpp

	src/vulkan/allocator.cpp
	src/vulkan/context.cpp
	src/vulkan/creation.cpp
	src/vulkan/gxm_to_vulkan.cpp
	src/vulkan/pipeline_cache.cpp
	src/vulkan/renderer.cpp
	src/vulkan/scene.cpp
	src/vulkan/overlay_renderer.cpp
	src/vulkan/screen_filters.cpp
	src/vulkan/screen_renderer.cpp
	src/vulkan/surface_cache.cpp
	src/vulkan/sync_state.cpp
	src/vulkan/texture.cpp

	src/texture/cache.cpp
	src/texture/format.cpp
	src/texture/palette.cpp
	src/texture/pvrt-dec.cpp
	src/texture/replacement.cpp
	src/texture/yuv.cpp

	src/batch.cpp
	src/creation.cpp
	src/renderer.cpp
	src/scene.cpp
	src/shaders.cpp
	src/state_set.cpp
	src/sync.cpp
	src/transfer.cpp
)

target_include_directories(renderer PUBLIC include)
target_link_libraries(renderer PUBLIC display mem stb shader glutil threads config util vkutil overlay)
target_link_libraries(renderer PRIVATE dialog ddspp SDL3::SDL3 stb ffmpeg xxHash::xxhash concurrentqueue)

if(ANDROID)
	target_link_libraries(renderer PRIVATE android adrenotools)
endif()

if(APPLE AND NOT IOS)
	target_sources(renderer PRIVATE src/vulkan/metal_layer.mm)
	target_link_libraries(renderer PRIVATE "-framework QuartzCore" "-framework Cocoa")
endif()

if(X11_FOUND)
	target_link_libraries(renderer PRIVATE ${X11_LIBRARIES})
	target_include_directories(renderer PRIVATE ${X11_INCLUDE_DIR})
endif()

if(WAYLAND_FOUND)
	target_link_libraries(renderer PRIVATE ${WAYLAND_LIBRARIES})
	target_include_directories(renderer PRIVATE ${WAYLAND_INCLUDE_DIRS})
endif()

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
	# silence all vma warnings
	target_compile_options(renderer PRIVATE "-Wno-nullability-completeness")
endif()

# Marshmallow Tracy linking
if(TRACY_ENABLE_ON_CORE_COMPONENTS)
	target_link_libraries(renderer PRIVATE tracy)
endif()

option(VITA3K_BUILD_GPU_TESTS "Build Vulkan surface cache regression tests (requires a GPU and glslc)" OFF)
if(BUILD_TESTING AND NOT IOS AND NOT ANDROID)
	add_executable(renderer-shader-cache-tests tests/shader_cache.cpp)
	target_link_libraries(renderer-shader-cache-tests PRIVATE renderer googletest)
	add_test(NAME renderer-shader-cache COMMAND renderer-shader-cache-tests)
endif()

if(BUILD_TESTING AND VITA3K_BUILD_GPU_TESTS AND NOT IOS AND NOT ANDROID)
	find_program(VITA3K_TEST_GLSLC glslc REQUIRED)
	set(DEPTH_TEST_SHADERS)
	foreach(ASPECT depth stencil)
		set(SPIRV_FILE "${CMAKE_CURRENT_BINARY_DIR}/depth-cache-${ASPECT}.spv")
		add_custom_command(OUTPUT "${SPIRV_FILE}"
			COMMAND "${VITA3K_TEST_GLSLC}" -fshader-stage=compute -DASPECT_${ASPECT}
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/depth_surface_sample.comp" -o "${SPIRV_FILE}"
			DEPENDS tests/depth_surface_sample.comp VERBATIM)
		list(APPEND DEPTH_TEST_SHADERS "${SPIRV_FILE}")
	endforeach()
	add_executable(renderer-depth-cache-tests tests/depth_surface_cache.cpp ${DEPTH_TEST_SHADERS})
	target_link_libraries(renderer-depth-cache-tests PRIVATE renderer concurrentqueue)
	add_test(NAME renderer-depth-cache COMMAND renderer-depth-cache-tests ${DEPTH_TEST_SHADERS})
	set_tests_properties(renderer-depth-cache PROPERTIES LABELS GPU)
endif()
