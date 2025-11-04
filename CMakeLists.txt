cmake_minimum_required(VERSION 4.0)
project(etsuko C)

set(CMAKE_C_STANDARD 17)

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
        src/secret.h
        src/ui_ex.h
        src/ui_ex.c
        src/renderer.h
        src/renderer.c)

target_compile_options(etsuko PRIVATE -Wall -Wpedantic)

find_package(SDL2 CONFIG REQUIRED)
target_link_libraries(etsuko
        PRIVATE
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)
find_package(SDL2_ttf CONFIG REQUIRED)
target_link_libraries(etsuko
        PRIVATE
        $<IF:$<TARGET_EXISTS:SDL2_ttf::SDL2_ttf>,SDL2_ttf::SDL2_ttf,SDL2_ttf::SDL2_ttf-static>
)
find_package(SDL2_mixer CONFIG REQUIRED)
target_link_libraries(etsuko
        PRIVATE
        $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
)

find_package(SDL2_image CONFIG REQUIRED)
target_link_libraries(etsuko
        PRIVATE
        $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>)

find_package(OpenGL REQUIRED)
target_link_libraries(etsuko PRIVATE OpenGL::GL)

target_compile_features(etsuko PRIVATE c_std_17)

if(EMSCRIPTEN)
    message(STATUS "Configuring for Emscripten/WebAssembly")

    set_target_properties(etsuko PROPERTIES SUFFIX ".html")

    target_link_options(etsuko PRIVATE
            "SHELL:-sALLOW_MEMORY_GROWTH=1"
            "SHELL:-sEXIT_RUNTIME=0"
            "SHELL:-O3"
            "SHELL:-dead_strip"
            "SHELL:-sASYNCIFY=1"
            "SHELL:-sFETCH=1"
            "SHELL:-sMIN_WEBGL_VERSION=2"
            "SHELL:-sMAX_WEBGL_VERSION=2"
            "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/shell_minimal.html"
            "SHELL:--use-preload-plugins"
            # Preload assets directory into virtual FS
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
    )

else()
    message(STATUS "Configuring regular desktop build")

    find_package(GLEW REQUIRED)
    target_link_libraries(etsuko PRIVATE GLEW::GLEW)

    target_compile_options(etsuko PRIVATE "-fsanitize=address,undefined")
    target_compile_options(etsuko PRIVATE "-g")
    target_link_options(etsuko PRIVATE "-fsanitize=address,undefined")
    target_link_options(etsuko PRIVATE "-Wl,-dead_strip")
    target_compile_options(etsuko PRIVATE "-g")

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
endif()

target_include_directories(etsuko PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${SDL2_INCLUDE_DIRS}
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(etsuko PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wno-unused-parameter
    )
endif()