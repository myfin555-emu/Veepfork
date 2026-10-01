add_library(
    audio
    STATIC
    src/audio.cpp
    src/impl/sdl_audio.cpp
)

if(NOT IOS)
	# Cubeb is not built for iOS in the initial port: SDL3 provides the audio
    # driver (CoreAudio). The factory below is conditioned on VITA3K_IOS_NO_CUBEB.
	target_sources(audio PRIVATE src/impl/cubeb_audio.cpp)
	target_link_libraries(audio PRIVATE cubeb)
else()
	target_compile_definitions(audio PRIVATE VITA3K_IOS_NO_CUBEB)
endif()

target_include_directories(audio PUBLIC include)
target_link_libraries(audio PRIVATE util SDL3::SDL3)
