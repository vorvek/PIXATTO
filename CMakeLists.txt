cmake_minimum_required(VERSION 3.24)

project(Pixelizer VERSION 1.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

if(WIN32)
    enable_language(RC)
    set(PIXELIZER_WINDOWS_ICON "${PROJECT_SOURCE_DIR}/assets/icon/pixelizer.ico")
    configure_file(
        src/platform/windows/pixelizer.rc.in
        "${PROJECT_BINARY_DIR}/pixelizer.rc"
        @ONLY
    )
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.4
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.7
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 31c1ad37456438565541f4919958214b6e762fb4
)

FetchContent_MakeAvailable(SDL3 imgui stb)

if(TARGET SDL3::SDL3-static)
    set(PIXELIZER_SDL_TARGET SDL3::SDL3-static)
elseif(TARGET SDL3::SDL3)
    set(PIXELIZER_SDL_TARGET SDL3::SDL3)
else()
    message(FATAL_ERROR "SDL3 was fetched, but no usable SDL3 CMake target was found.")
endif()

message(STATUS "Pixelizer SDL target: ${PIXELIZER_SDL_TARGET}")

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

target_include_directories(imgui
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC ${PIXELIZER_SDL_TARGET})

set(PIXELIZER_APP_SOURCES
    src/app.cpp
    src/file_command_pump.cpp
    src/image_io.cpp
    src/localization.cpp
    src/main.cpp
    src/palette.cpp
    src/processing_edit_session.cpp
    src/vendor/stb_impl.cpp
)

foreach(optional_source IN ITEMS
    src/image_processing.cpp
)
    if(EXISTS "${PROJECT_SOURCE_DIR}/${optional_source}")
        list(APPEND PIXELIZER_APP_SOURCES ${optional_source})
    endif()
endforeach()

if(WIN32)
    list(APPEND PIXELIZER_APP_SOURCES "${PROJECT_BINARY_DIR}/pixelizer.rc")
endif()

add_executable(pixelizer WIN32
    ${PIXELIZER_APP_SOURCES}
)

target_include_directories(pixelizer
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${stb_SOURCE_DIR}
)

target_link_libraries(pixelizer
    PRIVATE
        ${PIXELIZER_SDL_TARGET}
        imgui
)

add_custom_command(TARGET pixelizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:pixelizer>/assets"
)

if(MINGW)
    target_link_options(pixelizer PRIVATE -static -static-libgcc -static-libstdc++)
endif()

if(MSVC)
    target_compile_options(pixelizer PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixelizer PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(pixelizer_processing_edit_session_tests
    tests/processing_edit_session_tests.cpp
    src/processing_edit_session.cpp
)

target_include_directories(pixelizer_processing_edit_session_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(pixelizer_processing_edit_session_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixelizer_processing_edit_session_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME processing_edit_session COMMAND pixelizer_processing_edit_session_tests)

add_executable(pixelizer_file_command_pump_tests
    tests/file_command_pump_tests.cpp
    src/file_command_pump.cpp
)

target_include_directories(pixelizer_file_command_pump_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(pixelizer_file_command_pump_tests
    PRIVATE
        ${PIXELIZER_SDL_TARGET}
)

if(MSVC)
    target_compile_options(pixelizer_file_command_pump_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixelizer_file_command_pump_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME file_command_pump COMMAND pixelizer_file_command_pump_tests)

add_executable(pixelizer_localization_tests
    tests/localization_tests.cpp
    src/localization.cpp
)

target_include_directories(pixelizer_localization_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(pixelizer_localization_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixelizer_localization_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME localization COMMAND pixelizer_localization_tests)

add_executable(pixelizer_image_processing_tests
    tests/image_processing_tests.cpp
    src/image_processing.cpp
)

target_include_directories(pixelizer_image_processing_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(pixelizer_image_processing_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixelizer_image_processing_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME image_processing COMMAND pixelizer_image_processing_tests)
