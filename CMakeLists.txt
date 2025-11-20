cmake_minimum_required(VERSION 3.10)
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
        src/contrib/stb_image.c
        src/contrib/stb_truetype.c
        src/contrib/minimp3.c
        src/resource_includes.h)

set_source_files_properties(src/contrib/stb_image.c PROPERTIES COMPILE_FLAGS "-w")
set_source_files_properties(src/contrib/stb_truetype.c PROPERTIES COMPILE_FLAGS "-w")
set_source_files_properties(src/contrib/minimp3.c PROPERTIES COMPILE_FLAGS "-w")

target_compile_features(etsuko PRIVATE c_std_17)

if(EMSCRIPTEN)
    message(STATUS "Configuring for Emscripten/WebAssembly")

    set_target_properties(etsuko PROPERTIES SUFFIX ".html")

    target_compile_options(etsuko PRIVATE
            "SHELL:-sUSE_SDL=2"
            "SHELL:-sUSE_ICU=1"
    )

    target_link_options(etsuko PRIVATE
            "SHELL:-sUSE_SDL=2"
            "SHELL:-sUSE_GLFW=0"
            "SHELL:-sUSE_ICU=1"

            "SHELL:-sALLOW_MEMORY_GROWTH=1"
            "SHELL:-sEXIT_RUNTIME=0"
            "SHELL:-sASYNCIFY=1"
            "SHELL:-sFETCH=1"

            #"SHELL:-sMIN_WEBGL_VERSION=2"
            "SHELL:-sMAX_WEBGL_VERSION=2"

            "SHELL:-Oz"
            "SHELL:-flto"
            "SHELL:-g"
            "SHELL:-sFILESYSTEM=0"

            "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/shell_minimal.html"
            #"SHELL:--use-preload-plugins"
            #"SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
    )

else()
    message(STATUS "Configuring regular desktop build")

    find_package(SDL2 REQUIRED)
    target_link_libraries(etsuko
            PRIVATE
            $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
            $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    )

    find_package(OpenGL REQUIRED)
    target_link_libraries(etsuko PRIVATE OpenGL::GL)

    find_package(GLEW REQUIRED)
    target_link_libraries(etsuko PRIVATE GLEW::GLEW)

    # ICU
    find_path(ICU_INCLUDE_DIR unicode/utf8.h PATHS /opt/homebrew/opt/icu4c@78/include /usr/local/opt/icu4c/include)
    find_library(ICU_LIBRARY icuuc PATHS /opt/homebrew/opt/icu4c@78/lib /usr/local/opt/icu4c/lib)
    if (NOT ICU_INCLUDE_DIR OR NOT ICU_LIBRARY)
        message(FATAL_ERROR "ICU not found")
    endif()
    target_include_directories(etsuko PRIVATE ${ICU_INCLUDE_DIR})
    target_link_libraries(etsuko PRIVATE ${ICU_LIBRARY})

    # -lm
    target_link_libraries(etsuko PRIVATE m)

    target_compile_options(etsuko PRIVATE "-fsanitize=address,undefined")
    target_compile_options(etsuko PRIVATE "-g")
    target_link_options(etsuko PRIVATE "-fsanitize=address,undefined")
    target_link_options(etsuko PRIVATE "-Wl,-dead_strip")

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

target_include_directories(etsuko PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(etsuko PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wno-unused-parameter
    )
endif()
