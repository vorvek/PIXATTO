cmake_minimum_required(VERSION 3.24)

project(Pixatto VERSION 1.7.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
find_package(OpenGL REQUIRED)

if(WIN32)
    enable_language(RC)
    set(PIXATTO_WINDOWS_ICON "${PROJECT_SOURCE_DIR}/assets/icon/pixatto.ico")
    configure_file(
        src/platform/windows/pixatto.rc.in
        "${PROJECT_BINARY_DIR}/pixatto.rc"
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
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)

function(pixatto_enable_release_sections target)
    if(MSVC)
        return()
    endif()

    check_c_compiler_flag("-ffunction-sections" PIXATTO_HAS_C_FUNCTION_SECTIONS)
    check_cxx_compiler_flag("-ffunction-sections" PIXATTO_HAS_CXX_FUNCTION_SECTIONS)
    if(PIXATTO_HAS_C_FUNCTION_SECTIONS AND PIXATTO_HAS_CXX_FUNCTION_SECTIONS)
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:-ffunction-sections -fdata-sections>
        )
    endif()
endfunction()

function(pixatto_enable_release_dead_code_collection target)
    if(MSVC)
        return()
    endif()

    pixatto_enable_release_sections(${target})
    check_linker_flag(CXX "-Wl,--gc-sections" PIXATTO_HAS_GC_SECTIONS)
    if(PIXATTO_HAS_GC_SECTIONS)
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
    SDL3_image
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
    GIT_TAG release-3.4.2
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE
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

set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_STRICT ON CACHE BOOL "" FORCE)
set(SDLIMAGE_ANI OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BMP ON CACHE BOOL "" FORCE)
set(SDLIMAGE_GIF ON CACHE BOOL "" FORCE)
set(SDLIMAGE_JPG ON CACHE BOOL "" FORCE)
set(SDLIMAGE_JXL ON CACHE BOOL "" FORCE)
set(SDLIMAGE_LBM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PCX OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG ON CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNM ON CACHE BOOL "" FORCE)
set(SDLIMAGE_QOI ON CACHE BOOL "" FORCE)
set(SDLIMAGE_SVG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TGA ON CACHE BOOL "" FORCE)
set(SDLIMAGE_TIF ON CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP ON CACHE BOOL "" FORCE)
set(SDLIMAGE_XCF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XPM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XV OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF_SAVE OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BMP_SAVE OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_GIF_SAVE OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_JPG_SAVE OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_SAVE OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TGA_SAVE OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP_SAVE OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_DOXYGEN OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
set(jbig OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL3 imgui stb SDL3_image)
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
    set(PIXATTO_SDL_TARGET SDL3::SDL3-static)
elseif(TARGET SDL3::SDL3)
    set(PIXATTO_SDL_TARGET SDL3::SDL3)
else()
    message(FATAL_ERROR "SDL3 was fetched, but no usable SDL3 CMake target was found.")
endif()

message(STATUS "Pixatto SDL target: ${PIXATTO_SDL_TARGET}")

function(pixatto_link_platform_video_playback target)
    if(WIN32)
        target_link_libraries(${target} PRIVATE mfplat mfreadwrite mfuuid ole32)
    endif()
endfunction()

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

target_link_libraries(imgui PUBLIC ${PIXATTO_SDL_TARGET})

target_compile_definitions(imgui PUBLIC IMGUI_USE_WCHAR32)

pixatto_enable_release_sections(imgui)

add_library(ufbx STATIC
    ${ufbx_SOURCE_DIR}/ufbx.c
)

set_target_properties(ufbx PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

target_include_directories(ufbx
    PUBLIC
        ${ufbx_SOURCE_DIR}
)
pixatto_enable_release_sections(ufbx)

add_library(tinyxml2 STATIC
    ${tinyxml2_SOURCE_DIR}/tinyxml2.cpp
)

target_include_directories(tinyxml2
    PUBLIC
        ${tinyxml2_SOURCE_DIR}
)
pixatto_enable_release_sections(tinyxml2)

set(PIXATTO_APP_SOURCES
    src/app.cpp
    src/file_command_pump.cpp
    src/image_io.cpp
    src/localization.cpp
    src/main.cpp
    src/model.cpp
    src/model_renderer.cpp
    src/palette.cpp
    src/preset.cpp
    src/processing_edit_session.cpp
    src/video_backend.cpp
    src/video_playback_decoder.cpp
    src/vendor/stb_impl.cpp
)

foreach(optional_source IN ITEMS
    src/image_processing.cpp
)
    if(EXISTS "${PROJECT_SOURCE_DIR}/${optional_source}")
        list(APPEND PIXATTO_APP_SOURCES ${optional_source})
    endif()
endforeach()

if(WIN32)
    list(APPEND PIXATTO_APP_SOURCES "${PROJECT_BINARY_DIR}/pixatto.rc")
endif()

add_executable(pixatto WIN32
    ${PIXATTO_APP_SOURCES}
)

target_include_directories(pixatto
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${stb_SOURCE_DIR}
        ${tinygltf_SOURCE_DIR}
        ${tinyobjloader_SOURCE_DIR}
)

target_link_libraries(pixatto
    PRIVATE
        ${PIXATTO_SDL_TARGET}
        SDL3_image::SDL3_image
        imgui
        OpenGL::GL
        ufbx
        tinyxml2
)
pixatto_link_platform_video_playback(pixatto)

if(TARGET SDL3-static)
    pixatto_enable_release_sections(SDL3-static)
endif()

pixatto_enable_release_dead_code_collection(pixatto)

add_custom_command(TARGET pixatto POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "$<TARGET_FILE_DIR:pixatto>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:pixatto>/assets"
)

if(MINGW)
    target_link_options(pixatto PRIVATE -static -static-libgcc -static-libstdc++)
endif()

if(MSVC)
    target_compile_options(pixatto PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixatto PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(pixatto_processing_edit_session_tests
    tests/processing_edit_session_tests.cpp
    src/processing_edit_session.cpp
)

target_include_directories(pixatto_processing_edit_session_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(pixatto_processing_edit_session_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixatto_processing_edit_session_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME processing_edit_session COMMAND pixatto_processing_edit_session_tests)

add_executable(pixatto_file_command_pump_tests
    tests/file_command_pump_tests.cpp
    src/file_command_pump.cpp
    src/image_io.cpp
    src/image_processing.cpp
    src/video_backend.cpp
    src/vendor/stb_impl.cpp
)

target_include_directories(pixatto_file_command_pump_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${stb_SOURCE_DIR}
)

target_link_libraries(pixatto_file_command_pump_tests
    PRIVATE
        ${PIXATTO_SDL_TARGET}
        SDL3_image::SDL3_image
)

if(MSVC)
    target_compile_options(pixatto_file_command_pump_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixatto_file_command_pump_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME file_command_pump COMMAND pixatto_file_command_pump_tests)

add_executable(pixatto_video_backend_tests
    tests/video_backend_tests.cpp
    src/image_io.cpp
    src/image_processing.cpp
    src/video_backend.cpp
    src/vendor/stb_impl.cpp
)

target_include_directories(pixatto_video_backend_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${stb_SOURCE_DIR}
)

target_link_libraries(pixatto_video_backend_tests
    PRIVATE
        ${PIXATTO_SDL_TARGET}
        SDL3_image::SDL3_image
)

if(MSVC)
    target_compile_options(pixatto_video_backend_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixatto_video_backend_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME video_backend COMMAND pixatto_video_backend_tests)

add_executable(pixatto_video_ffmpeg_integration_tests
    tests/video_ffmpeg_integration_tests.cpp
    src/image_io.cpp
    src/image_processing.cpp
    src/video_backend.cpp
    src/video_playback_decoder.cpp
    src/vendor/stb_impl.cpp
)

target_include_directories(pixatto_video_ffmpeg_integration_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${stb_SOURCE_DIR}
)

target_link_libraries(pixatto_video_ffmpeg_integration_tests
    PRIVATE
        ${PIXATTO_SDL_TARGET}
        SDL3_image::SDL3_image
)
pixatto_link_platform_video_playback(pixatto_video_ffmpeg_integration_tests)

if(MSVC)
    target_compile_options(pixatto_video_ffmpeg_integration_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixatto_video_ffmpeg_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME video_ffmpeg_integration COMMAND pixatto_video_ffmpeg_integration_tests)
set_tests_properties(video_ffmpeg_integration PROPERTIES SKIP_RETURN_CODE 77)

add_executable(pixatto_image_io_tests
    tests/image_io_tests.cpp
    src/image_io.cpp
    src/vendor/stb_impl.cpp
)

target_include_directories(pixatto_image_io_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${stb_SOURCE_DIR}
)

target_link_libraries(pixatto_image_io_tests
    PRIVATE
        ${PIXATTO_SDL_TARGET}
        SDL3_image::SDL3_image
)

if(MSVC)
    target_compile_options(pixatto_image_io_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixatto_image_io_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME image_io COMMAND pixatto_image_io_tests)

add_executable(pixatto_model_tests
    tests/model_tests.cpp
    src/image_io.cpp
    src/model.cpp
    src/vendor/stb_impl.cpp
)

target_include_directories(pixatto_model_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${stb_SOURCE_DIR}
        ${tinygltf_SOURCE_DIR}
        ${tinyobjloader_SOURCE_DIR}
        ${ufbx_SOURCE_DIR}
        ${tinyxml2_SOURCE_DIR}
)

target_link_libraries(pixatto_model_tests
    PRIVATE
        ${PIXATTO_SDL_TARGET}
        SDL3_image::SDL3_image
        ufbx
        tinyxml2
)

if(MSVC)
    target_compile_options(pixatto_model_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixatto_model_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME model COMMAND pixatto_model_tests)

add_executable(pixatto_localization_tests
    tests/localization_tests.cpp
    src/localization.cpp
)

target_include_directories(pixatto_localization_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(pixatto_localization_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixatto_localization_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME localization COMMAND pixatto_localization_tests)

add_executable(pixatto_preset_tests
    tests/preset_tests.cpp
    src/preset.cpp
)

target_include_directories(pixatto_preset_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(pixatto_preset_tests
    PRIVATE
        ${PIXATTO_SDL_TARGET}
)

if(MSVC)
    target_compile_options(pixatto_preset_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixatto_preset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME presets COMMAND pixatto_preset_tests)

add_executable(pixatto_image_processing_tests
    tests/image_processing_tests.cpp
    src/image_processing.cpp
)

target_include_directories(pixatto_image_processing_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(pixatto_image_processing_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pixatto_image_processing_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME image_processing COMMAND pixatto_image_processing_tests)
