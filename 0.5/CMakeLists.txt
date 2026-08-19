cmake_minimum_required(VERSION 3.24)

project(
    PCkey
    VERSION 0.4.4
    DESCRIPTION "A lightweight VIA/QMK-style keyboard remapper for Windows"
    LANGUAGES CXX
)

include(CTest)
include(cmake/CompilerWarnings.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(
        CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(pckey_common STATIC
    src/common/config_store.cpp
    src/common/mapping_engine.cpp
    src/common/profile.cpp
)

target_include_directories(pckey_common
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(pckey_common
    PUBLIC
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

pckey_enable_warnings(pckey_common)

add_executable(PCkeyCore WIN32
    src/resources/pckey.rc
    src/core/main.cpp
    src/core/core_app.cpp
    src/core/input_injector.cpp
    src/core/keyboard_hook.cpp
    src/core/tray_icon.cpp
)

target_include_directories(PCkeyCore
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/resources"
)

target_link_libraries(PCkeyCore
    PRIVATE
        pckey_common
        user32
        shell32
)

pckey_enable_warnings(PCkeyCore)

add_executable(PCkeyEditor WIN32
    src/resources/pckey.rc
    src/editor/main.cpp
    src/editor/combo_editor.cpp
    src/editor/ipc_client.cpp
    src/editor/key_test_window.cpp
    src/editor/macro_editor.cpp
    src/editor/mouse_settings_editor.cpp
    src/editor/override_editor.cpp
    src/editor/tap_dance_editor.cpp
    src/editor/main_window.cpp
)

target_include_directories(PCkeyEditor
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/editor"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/resources"
)

target_link_libraries(PCkeyEditor
    PRIVATE
        pckey_common
        d2d1
        dwrite
        ole32
        user32
        gdi32
)

pckey_enable_warnings(PCkeyEditor)

if(BUILD_TESTING)
    add_executable(pckey_tests
        tests/mapping_engine_tests.cpp
    )

    target_link_libraries(pckey_tests
        PRIVATE
            pckey_common
    )

    pckey_enable_warnings(pckey_tests)

    add_test(NAME mapping_engine COMMAND pckey_tests)
endif()
