cmake_minimum_required(VERSION 3.21)
project(Whiteprint VERSION 0.2.0 LANGUAGES CXX)

# Windows port of Whiteprint. The macOS sources (../Sources, ../Package.swift) are the reference and
# are never modified from here. See ../docs/WINDOWS_PORT.md for the module map and conventions.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR OFF)

set(WP_VERSION "${PROJECT_VERSION}" CACHE STRING "Version shown in the app and the installer")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network PrintSupport Pdf Test)

enable_testing()

# wp_module(<name> [DEPS <modules>...] [QT <Qt6 components>...])
# Builds the static library wp_<name> from every .h/.cpp in <name>/ (not in <name>/tests/).
# Headers are included from the windows/ root, e.g. #include "core/Note.h".
function(wp_module name)
    cmake_parse_arguments(M "" "" "DEPS;QT" ${ARGN})
    file(GLOB srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.h)
    add_library(wp_${name} STATIC ${srcs})
    target_include_directories(wp_${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    set(qt Qt6::Core)
    foreach(c ${M_QT})
        list(APPEND qt Qt6::${c})
    endforeach()
    target_link_libraries(wp_${name} PUBLIC ${qt})
    foreach(d ${M_DEPS})
        target_link_libraries(wp_${name} PUBLIC wp_${d})
    endforeach()
    if(MSVC)
        target_compile_options(wp_${name} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(wp_${name} PRIVATE -Wall -Wextra)
    endif()
    # <name>/tests/*.cpp: one Qt Test executable per file, named wp_<name>_<file>.
    file(GLOB tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${name}/tests/*.cpp)
    foreach(t ${tests})
        get_filename_component(tn ${t} NAME_WE)
        set(exe wp_test_${name}_${tn})
        add_executable(${exe} ${t})
        target_link_libraries(${exe} PRIVATE wp_${name} Qt6::Test)
        add_test(NAME ${name}.${tn} COMMAND ${exe})
    endforeach()
endfunction()

wp_module(core)
wp_module(extract QT Gui Pdf DEPS core)
wp_module(render QT Gui Widgets PrintSupport Pdf DEPS core)
wp_module(bridge QT Network DEPS core)
wp_module(study QT Network DEPS core extract)
wp_module(editor QT Gui Widgets DEPS core render)

add_executable(whiteprint-mcp mcp/main.cpp)
target_link_libraries(whiteprint-mcp PRIVATE wp_core wp_bridge)
target_compile_definitions(whiteprint-mcp PRIVATE WP_VERSION="${WP_VERSION}")

file(GLOB app_srcs CONFIGURE_DEPENDS app/*.cpp app/*.h)
add_executable(Whiteprint WIN32 ${app_srcs} app/Whiteprint.rc app/resources.qrc)
target_link_libraries(Whiteprint PRIVATE wp_core wp_extract wp_render wp_editor wp_bridge wp_study
    Qt6::Widgets Qt6::Gui Qt6::Network Qt6::PrintSupport Qt6::Pdf)
target_compile_definitions(Whiteprint PRIVATE WP_VERSION="${WP_VERSION}")
file(GLOB app_tests CONFIGURE_DEPENDS app/tests/*.cpp)
if(app_tests)
    # App logic that the tests reach is compiled into a library without main.cpp.
    list(FILTER app_srcs EXCLUDE REGEX "main\.cpp$")
    add_library(wp_applogic STATIC ${app_srcs})
    target_include_directories(wp_applogic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(wp_applogic PUBLIC wp_core wp_extract wp_render wp_editor wp_bridge wp_study Qt6::Widgets)
    foreach(t ${app_tests})
        get_filename_component(tn ${t} NAME_WE)
        add_executable(wp_test_app_${tn} ${t})
        target_link_libraries(wp_test_app_${tn} PRIVATE wp_applogic Qt6::Test)
        add_test(NAME app.${tn} COMMAND wp_test_app_${tn})
    endforeach()
endif()

# Install rules (used by installer/package.ps1; Qt runtime is added by windeployqt).
install(TARGETS Whiteprint whiteprint-mcp RUNTIME DESTINATION .)
