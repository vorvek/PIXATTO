cmake_minimum_required(VERSION 3.24)

project(Pixelizer VERSION 1.6.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
find_package(OpenGL REQUIRED)

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
set(SDL_VIDEO ON CACHE BOOL "" FORCE)
set(SDL_OPENGL ON CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_RENDER_D3D12 OFF CACHE BOOL "" FORCE)
set(SDL_RENDER_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)

function(pixelizer_enable_release_sections target)
    if(MSVC)
        return()
    endif()

    check_c_compiler_flag("-ffunction-sections" PIXELIZER_HAS_C_FUNCTION_SECTIONS)
    check_cxx_compiler_flag("-ffunction-sections" PIXELIZER_HAS_CXX_FUNCTION_SECTIONS)
    if(PIXELIZER_HAS_C_FUNCTION_SECTIONS AND PIXELIZER_HAS_CXX_FUNCTION_SECTIONS)
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:-ffunction-sections -fdata-sections>
        )
    endif()
endfunction()

function(pixelizer_enable_release_dead_code_collection target)
    if(MSVC)
        return()
    endif()

    pixelizer_enable_release_sections(${target})
    check_linker_flag(CXX "-Wl,--gc-sections" PIXELIZER_HAS_GC_SECTIONS)
    if(PIXELIZER_HAS_GC_SECTIONS)
        target_link_options(${target} PRIVATE
            $<$<CONFIG:Release>:-Wl,--gc-sections>
        )
    endif()
endfunction()

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

FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.7
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG v2.0.0rc13
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_TAG v0.21.3
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG 11.0.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(SDL3 imgui stb)
if(POLICY CMP0169)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(tinygltf)
if(NOT tinygltf_POPULATED)
    FetchContent_Populate(tinygltf)
endif()
FetchContent_GetProperties(tinyobjloader)
if(NOT tinyobjloader_POPULATED)
    FetchContent_Populate(tinyobjloader)
endif()
FetchContent_GetProperties(ufbx)
if(NOT ufbx_POPULATED)
    FetchContent_Populate(ufbx)
endif()
FetchContent_GetProperties(tinyxml2)
if(NOT tinyxml2_POPULATED)
    FetchContent_Populate(tinyxml2)
endif()
if(POLICY CMP0169)
    cmake_policy(POP)
endif()

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
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC ${PIXELIZER_SDL_TARGET})
pixelizer_enable_release_sections(imgui)

add_library(ufbx STATIC
    ${ufbx_SOURCE_DIR}/ufbx.c
)

set_target_properties(ufbx PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

target_include_directories(ufbx
    PUBLIC
        ${ufbx_SOURCE_DIR}
)
pixelizer_enable_release_sections(ufbx)

add_library(tinyxml2 STATIC
    ${tinyxml2_SOURCE_DIR}/tinyxml2.cpp
)

target_include_directories(tinyxml2
    PUBLIC
        ${tinyxml2_SOURCE_DIR}
)
pixelizer_enable_release_sections(tinyxml2)

set(PIXELIZER_APP_SOURCES
    src/app.cpp
    src/file_command_pump.cpp
    src/image_io.cpp
    src/localization.cpp
    src/main.cpp
    src/model.cpp
    src/model_renderer.cpp
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
        ${tinygltf_SOURCE_DIR}
        ${tinyobjloader_SOURCE_DIR}
)

target_link_libraries(pixelizer
    PRIVATE
        ${PIXELIZER_SDL_TARGET}
        imgui
        OpenGL::GL
        ufbx
        tinyxml2
)

if(TARGET SDL3-static)
    pixelizer_enable_release_sections(SDL3-static)
endif()

pixelizer_enable_release_dead_code_collection(pixelizer)

add_custom_command(TARGET pixelizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "$<TARGET_FILE_DIR:pixelizer>/assets"
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

add_executable(pixelizer_image_io_tests
    tests/image_io_tests.cpp
    src/image_io.cpp
    src/vendor/stb_impl.cpp
)

target_include_directories(pixelizer_image_io_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${stb_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(pixelizer_image_io_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixelizer_image_io_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME image_io COMMAND pixelizer_image_io_tests)

add_executable(pixelizer_model_tests
    tests/model_tests.cpp
    src/image_io.cpp
    src/model.cpp
    src/vendor/stb_impl.cpp
)

target_include_directories(pixelizer_model_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${stb_SOURCE_DIR}
        ${tinygltf_SOURCE_DIR}
        ${tinyobjloader_SOURCE_DIR}
        ${ufbx_SOURCE_DIR}
        ${tinyxml2_SOURCE_DIR}
)

target_link_libraries(pixelizer_model_tests
    PRIVATE
        ufbx
        tinyxml2
)

if(MSVC)
    target_compile_options(pixelizer_model_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixelizer_model_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME model COMMAND pixelizer_model_tests)

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
