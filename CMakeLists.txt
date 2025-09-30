cmake_minimum_required(VERSION 4.0)
project(etsuko)

set(CMAKE_CXX_STANDARD 23)

add_executable(etsuko src/main.cpp
        src/karaoke.cpp
        src/karaoke.h
        src/audio.cpp
        src/audio.h
        src/renderer.h
        src/parser.cpp
        src/parser.h
        src/renderer.cpp
        src/repository.cpp
        src/repository.h
        src/events.cpp
        src/events.h
        src/common.h)

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
target_link_libraries(etsuko PRIVATE $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>)

target_compile_features(etsuko PRIVATE cxx_std_23)

if(EMSCRIPTEN)
    message(STATUS "Configuring for Emscripten/WebAssembly")

    set_target_properties(etsuko PROPERTIES SUFFIX ".html")

    target_link_options(etsuko PRIVATE
            "SHELL:-sALLOW_MEMORY_GROWTH=1"
            "SHELL:-sASSERTIONS=1"
            "SHELL:-sEXIT_RUNTIME=0"
            "SHELL:-O3"
            "SHELL:-sASYNCIFY=0"
            "SHELL:-sFETCH=1"
            "SHELL:-sFULL_ES3=1"
            "SHELL:-sUSE_WEBGL2=1"
            "SHELL:-sNO_DISABLE_EXCEPTION_CATCHING=1"
            "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/shell_minimal.html"
            "SHELL:-fexceptions"
            "SHELL:-sASSERTIONS=1"
            "SHELL:--profiling-funcs"
            "SHELL:--use-preload-plugins"
            # Preload assets directory into virtual FS
            #"SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
    )

else()
    message(STATUS "Configuring regular desktop build")

    find_package(OpenAL CONFIG REQUIRED)
    target_link_libraries(etsuko PRIVATE OpenAL::OpenAL)

    target_link_options(etsuko PRIVATE "-Wl,-dead_strip")

    add_custom_command(TARGET etsuko POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:etsuko>/assets
    )
endif()
