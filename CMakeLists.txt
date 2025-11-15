cmake_minimum_required(VERSION 3.5)
project(etsuko C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(etsuko src/main.c
        src/etsuko.h
        src/karaoke.h
        src/config.h
        src/config.c
        src/karaoke.c
        src/repository.h
        src/repository.c
        src/song.h
        src/etsuko.c
        src/ui.h
        src/ui.c
        src/song.c
        src/str_utils.h
        src/str_utils.c
        src/container_utils.h
        src/container_utils.c
        src/events.h
        src/events.c
        src/error.h
        src/error.c
        src/audio.h
        src/audio.c
        src/constants.h
        src/ui_ex.h
        src/ui_ex.c
        src/renderer.h
        src/renderer.c
        src/contrib/incbin.h)

target_compile_features(etsuko PRIVATE c_std_17)

if(EMSCRIPTEN)
    message(STATUS "Configuring for Emscripten/WebAssembly")

    set_target_properties(etsuko PROPERTIES SUFFIX ".html")

    # Use Emscripten's built-in SDL2 ports instead of vcpkg
    target_compile_options(etsuko PRIVATE
            "SHELL:-sUSE_SDL=2"
            "SHELL:-sUSE_SDL_TTF=2"
            "SHELL:-sUSE_SDL_MIXER=2"
            "SHELL:-sUSE_SDL_IMAGE=2"
            "SHELL:-sSDL2_IMAGE_FORMATS=['png','jpg']"
            "SHELL:-sSDL2_MIXER_FORMATS=['mp3']"
    )

    target_link_options(etsuko PRIVATE
            # Use built-in SDL2 ports
            "SHELL:-sUSE_SDL=2"
            "SHELL:-sUSE_SDL_TTF=2"
            "SHELL:-sUSE_SDL_MIXER=2"
            "SHELL:-sUSE_SDL_IMAGE=2"
            "SHELL:-sSDL2_IMAGE_FORMATS=['png','jpg']"
            "SHELL:-sSDL2_MIXER_FORMATS=['mp3']"

            # Memory and runtime settings
            "SHELL:-sALLOW_MEMORY_GROWTH=1"
            "SHELL:-sEXIT_RUNTIME=0"
            "SHELL:-sASYNCIFY=1"
            "SHELL:-sFETCH=1"

            # WebGL settings
            #"SHELL:-sMIN_WEBGL_VERSION=2"
            "SHELL:-sMAX_WEBGL_VERSION=2"

            # Optimization flags for size reduction
            "SHELL:-Oz"
            "SHELL:-flto"
            "SHELL:-g0"
            "SHELL:--closure 1"
            "SHELL:-sASSERTIONS=0"
            "SHELL:-sSTACK_SIZE=65536"

            # Shell and assets
            "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/shell_minimal.html"
            "SHELL:--use-preload-plugins"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
    )

else()
    message(STATUS "Configuring regular desktop build")

    # Desktop: Use vcpkg's SDL2 libraries
    find_package(SDL2 REQUIRED)
    target_link_libraries(etsuko
            PRIVATE
            $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
            $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    )

    find_package(SDL2_ttf REQUIRED)
    target_link_libraries(etsuko
            PRIVATE
            $<IF:$<TARGET_EXISTS:SDL2_ttf::SDL2_ttf>,SDL2_ttf::SDL2_ttf,SDL2_ttf::SDL2_ttf-static>
    )

    find_package(SDL2_mixer REQUIRED)
    target_link_libraries(etsuko
            PRIVATE
            $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
    )

    find_package(SDL2_image REQUIRED)
    target_link_libraries(etsuko
            PRIVATE
            $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>)

    # Desktop: OpenGL and GLEW
    find_package(OpenGL REQUIRED)
    target_link_libraries(etsuko PRIVATE OpenGL::GL)

    find_package(GLEW REQUIRED)
    target_link_libraries(etsuko PRIVATE GLEW::GLEW)

    # -lm
    target_link_libraries(etsuko PRIVATE m)

    # Desktop: Sanitizers and debug options
    target_compile_options(etsuko PRIVATE "-fsanitize=address,undefined")
    target_compile_options(etsuko PRIVATE "-g")
    target_link_options(etsuko PRIVATE "-fsanitize=address,undefined")
    target_link_options(etsuko PRIVATE "-Wl,-dead_strip")

    # Copy assets to build directory
    add_custom_command(TARGET etsuko POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:etsuko>/assets
    )

    add_custom_command(TARGET etsuko POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets_dbg
            $<TARGET_FILE_DIR:etsuko>/assets
    )

    target_include_directories(etsuko PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${SDL2_INCLUDE_DIRS}
    )
endif()

# Common include directories
target_include_directories(etsuko PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Common compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(etsuko PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wno-unused-parameter
    )
endif()
