cmake_minimum_required(VERSION 3.14)
project(etsuko C)

set(CMAKE_C_STANDARD 23)
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

if (EMSCRIPTEN)
    message(STATUS "Configuring for Emscripten/WebAssembly")

    set_target_properties(etsuko PROPERTIES SUFFIX ".html")

    target_compile_options(etsuko PRIVATE
            "SHELL:-Oz"
            "SHELL:-g0"
    )

    target_link_options(etsuko PRIVATE
            "SHELL:-sUSE_GLFW=3"
            "SHELL:-lopenal"

            "SHELL:-sALLOW_MEMORY_GROWTH=1"
            "SHELL:-sEXIT_RUNTIME=0"
            "SHELL:-sASYNCIFY=0"
            "SHELL:-sFETCH=1"

            #"SHELL:-sMIN_WEBGL_VERSION=2"
            "SHELL:-sMAX_WEBGL_VERSION=2"

            "SHELL:-Oz"
            "SHELL:-g0"
            "SHELL:-flto"
            "SHELL:-sFILESYSTEM=0"
            "SHELL:--closure=1"

            "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/shell_minimal.html"
    )

else ()
    message(STATUS "Configuring regular desktop build")

    find_package(glfw3 REQUIRED)
    target_link_libraries(etsuko PRIVATE glfw)

    find_package(OpenAL REQUIRED)
    target_link_libraries(etsuko PRIVATE OpenAL::OpenAL)

    find_package(OpenGL REQUIRED)
    target_link_libraries(etsuko PRIVATE OpenGL::GL)

    find_package(GLEW REQUIRED)
    target_link_libraries(etsuko PRIVATE GLEW::GLEW)

    # -lm
    target_link_libraries(etsuko PRIVATE m)

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(etsuko PRIVATE "-fsanitize=address,undefined")
        target_link_options(etsuko PRIVATE "-fsanitize=address,undefined")
        target_compile_options(etsuko PRIVATE "-g")
    else ()
        target_compile_options(etsuko PRIVATE "-g0")
    endif ()

    add_custom_command(TARGET etsuko POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:etsuko>/assets
    )

    if (EXISTS ${CMAKE_SOURCE_DIR}/assets_dbg)
        add_custom_command(TARGET etsuko POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/assets_dbg
                $<TARGET_FILE_DIR:etsuko>/assets
        )
    else ()
        message(STATUS "assets_dbg directory not found, skipping")
    endif ()

    target_include_directories(etsuko PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )
endif ()

target_include_directories(etsuko PRIVATE ${CMAKE_SOURCE_DIR}/src)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(etsuko PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wno-unused-parameter
    )
endif ()
