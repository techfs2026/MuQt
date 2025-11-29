cmake_minimum_required(VERSION 3.19)
project(MuQt LANGUAGES CXX)

# -----------------------------
# Global settings
# -----------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt installation path
set(CMAKE_PREFIX_PATH "D:/Qt6/6.10.0/msvc2022_64")

# Install output directory
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/deploy")

# Qt automatic MOC/RCC/UIC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

# -----------------------------
# Find Qt
# -----------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Concurrent)

if(WIN32)
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
endif()

# -----------------------------
# Third-party: MuPDF
# -----------------------------
set(MUPDF_DIR "D:/Ext-Lib/mupdf-win-x64")
set(MUPDF_INCLUDE_DIR "${MUPDF_DIR}/include")

# Multi-config Debug/Release lib directories
set(MUPDF_LIB_DEBUG   "${MUPDF_DIR}/lib/Debug")
set(MUPDF_LIB_RELEASE "${MUPDF_DIR}/lib/Release")

# -----------------------------
# Collect all source files automatically
# -----------------------------
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc"
)

# 排除 autogen 和 build 目录
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "${CMAKE_CURRENT_BINARY_DIR}")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX ".*_autogen.*")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX ".*/CMakeFiles/.*")

# -----------------------------
# Create the executable target
# -----------------------------
qt_standard_project_setup()

qt_add_executable(MuQt
    WIN32 MACOSX_BUNDLE
    ${PROJECT_SOURCES}

    ${APP_ICON_RESOURCE_WINDOWS}
)

# -----------------------------
# Include directories
# -----------------------------
target_include_directories(MuQt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/session
    ${CMAKE_CURRENT_SOURCE_DIR}/model
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/util
    ${CMAKE_CURRENT_SOURCE_DIR}/tool
    ${CMAKE_CURRENT_SOURCE_DIR}/manager
    ${CMAKE_CURRENT_SOURCE_DIR}/handler

    ${MUPDF_INCLUDE_DIR}
)

# -----------------------------
# Link directories (Debug/Release)
# -----------------------------
target_link_directories(MuQt PRIVATE
    "$<$<CONFIG:Debug>:${MUPDF_LIB_DEBUG}>"
    "$<$<CONFIG:Release>:${MUPDF_LIB_RELEASE}>"
)

# -----------------------------
# Link libraries
# -----------------------------
target_link_libraries(MuQt PRIVATE
    Qt::Core
    Qt::Widgets
    Qt6::Concurrent


    "$<$<CONFIG:Debug>:libmupdf.lib;libthirdparty.lib>"
    "$<$<CONFIG:Release>:libmupdf.lib;libthirdparty.lib>"
)

# -----------------------------
# Install section
# -----------------------------
include(GNUInstallDirs)

install(TARGETS MuQt
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# -----------------------------
# Qt deployment
# -----------------------------
qt_generate_deploy_app_script(
    TARGET MuQt
    OUTPUT_SCRIPT deploy_script
)
install(SCRIPT ${deploy_script})
