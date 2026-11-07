cmake_minimum_required(VERSION 3.21)
project(guikit_mobile VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if (MSVC)
    add_compile_options(/utf-8 /Zc:__cplusplus /MP)
elseif (ANDROID)
    add_compile_options(-Wno-format-security)
endif()

option(GUIKIT_MOBILE_BUILD_DEMO "Build GuiKit Mobile interactive sensor/camera workstation demo" ${PROJECT_IS_TOP_LEVEL})
option(GUIKIT_MOBILE_BUILD_TESTS "Build GuiKit Mobile unit tests" ${PROJECT_IS_TOP_LEVEL})

# ==============================================================================
# 1. Submodules: third_party/guikit & third_party/mobile (keel::mobile HAL)
# ==============================================================================
set(GUIKIT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(GUIKIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GUIKIT_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/guikit)

set(KEEL_MOBILE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/mobile EXCLUDE_FROM_ALL)

# ==============================================================================
# 2. Module: guikit::mobile (UI & SDL3 Bridge over keel::mobile HAL)
# ==============================================================================
set(GUIKIT_MOBILE_SOURCES
    src/guikit/mobile/sensors.cpp
    src/guikit/mobile/camera.cpp
    src/guikit/mobile/location.cpp
    src/guikit/mobile/system_bridge.cpp
    src/guikit/mobile/touch_gesture.cpp
    src/guikit/mobile/mobile_runtime.cpp
    src/guikit/mobile/mobile_window.cpp
    src/guikit/mobile/sdl_mobile_window_backend.cpp
)

add_library(guikit_mobile STATIC ${GUIKIT_MOBILE_SOURCES})
add_library(guikit::mobile ALIAS guikit_mobile)

target_compile_features(guikit_mobile PUBLIC cxx_std_20)
target_include_directories(guikit_mobile PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(guikit_mobile PUBLIC guikit::guikit keel::mobile)

# ==============================================================================
# 3. Interactive Showcase Application (Android Shared Library / Desktop App)
# ==============================================================================
if (GUIKIT_MOBILE_BUILD_DEMO)
    if (ANDROID)
        # SDL3 Android SDLActivity loads libmain.so entry point
        add_library(main SHARED
            examples/mobile_demo/main.cpp
        )
        target_link_libraries(main PRIVATE guikit::mobile)
    else()
        add_executable(guikit_mobile_demo
            examples/mobile_demo/main.cpp
        )
        target_link_libraries(guikit_mobile_demo PRIVATE guikit::mobile)

        # Copy embedded assets folder beside build directory for file-based chart lookups
        add_custom_command(TARGET guikit_mobile_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/guikit/assets"
                "${CMAKE_CURRENT_BINARY_DIR}/assets"
            COMMENT "Syncing GuiKit assets to build directory"
        )
    endif()
endif()

# ==============================================================================
# 4. Automated Unit Tests
# ==============================================================================
if (GUIKIT_MOBILE_BUILD_TESTS AND NOT ANDROID)
    enable_testing()

    add_executable(test_guikit_mobile
        tests/test_mobile.cpp
    )
    set_target_properties(test_guikit_mobile PROPERTIES OUTPUT_NAME "test_mobile")
    target_link_libraries(test_guikit_mobile PRIVATE guikit::mobile)
    add_test(NAME mobile_hal COMMAND test_guikit_mobile)
endif()
