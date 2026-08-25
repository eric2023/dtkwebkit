cmake_minimum_required(VERSION 3.13)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FILE_VERSION)
string(STRIP "${FILE_VERSION}" FILE_VERSION)

project(dtkwebkit
    VERSION ${FILE_VERSION}
    DESCRIPTION "WPE WebKit Qt6 embedding layer for UnionTech UOS"
    HOMEPAGE_URL "https://github.com/linuxdeepin/dtkwebkit"
    LANGUAGES CXX C
)

# DTK standard: default install /usr
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX /usr)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(FindPkgConfig)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt6
find_package(Qt6 COMPONENTS Core Gui OpenGL OpenGLWidgets Widgets Test REQUIRED)

# DTK6
find_package(Dtk6Core REQUIRED)
find_package(Dtk6Widget REQUIRED)

# WPE WebKit + Backend-FDO (pkg-config)
# Target: wpe-webkit-2.0 >= 2.50 (per spec V3.1 §9.3)
# Current dev env has 2.46.3; API is compatible, 2.50+ adds WPEPlatform
pkg_check_modules(WPE REQUIRED IMPORTED_TARGET
    wpe-webkit-2.0>=2.46
    wpe-1.0>=1.16
    wpebackend-fdo-1.0>=1.12
)

# EGL + GLES (pkg-config)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GLES2 REQUIRED IMPORTED_TARGET glesv2)

# GStreamer (media backend)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0>=1.20
    gstreamer-plugins-base-1.0>=1.20
)

# GLib + libsoup (WPE dependency)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gobject-2.0 gio-2.0)

# --- Library target ---
add_library(dtkwebkit SHARED
    src/wpe_view.cpp
    src/wpe_event_translator.cpp
    src/wpe_scheme_handler.cpp
    src/wpe_bridge.cpp
    src/wpe_channel_adapter.cpp
)

target_include_directories(dtkwebkit
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/dtkwebkit>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/dtkwebkit>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(dtkwebkit PRIVATE QT_NO_KEYWORDS)

target_link_libraries(dtkwebkit
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Widgets
        Dtk6::Core
        Dtk6::Widget
    PUBLIC
        PkgConfig::WPE
        PkgConfig::EGL
        PkgConfig::GLES2
        PkgConfig::GLIB
    PRIVATE
        PkgConfig::GST
)

# --- Install ---
install(TARGETS dtkwebkit
    EXPORT dtkwebkit-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/dtkwebkit
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# --- CMake Package Config ---
install(EXPORT dtkwebkit-targets
    FILE dtkwebkit-targets.cmake
    NAMESPACE dtkwebkit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dtkwebkit
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dtkwebkit-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/dtkwebkit-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dtkwebkit
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/dtkwebkit-config-version.cmake
    VERSION ${FILE_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/dtkwebkit-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/dtkwebkit-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dtkwebkit
)

# --- Examples ---
add_subdirectory(examples/minibrowser)

# --- Tests ---
enable_testing()
add_subdirectory(tests)
