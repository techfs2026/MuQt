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
# Third-party: OpenCV
# -----------------------------
set(OPENCV_DIR "D:/Ext-Lib/opencv-win-x64")
set(OPENCV_INCLUDE_DIR "${OPENCV_DIR}/include")

# Multi-config Debug/Release lib directories
set(OPENCV_LIB_DEBUG   "${OPENCV_DIR}/lib/Debug")
set(OPENCV_LIB_RELEASE "${OPENCV_DIR}/lib/Release")

# DLL 目录（用于运行时拷贝）
set(OPENCV_BIN_DEBUG   "${OPENCV_DIR}/bin/Debug")
set(OPENCV_BIN_RELEASE "${OPENCV_DIR}/bin/Release")

# -----------------------------
# Third-party: onnxruntime
# -----------------------------
set(ONNXRUNTIME_DIR "D:/Ext-Lib/onnxruntime-win-x64")
set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_DIR}/include")
set(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_DIR}/lib")
set(ONNXRUNTIME_BIN_DIR "${ONNXRUNTIME_DIR}/lib")

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
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ocr
    ${CMAKE_CURRENT_SOURCE_DIR}/session
    ${CMAKE_CURRENT_SOURCE_DIR}/model
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/util
    ${CMAKE_CURRENT_SOURCE_DIR}/tool
    ${CMAKE_CURRENT_SOURCE_DIR}/manager
    ${CMAKE_CURRENT_SOURCE_DIR}/handler
    ${CMAKE_CURRENT_SOURCE_DIR}/ocr
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty

    ${MUPDF_INCLUDE_DIR}
    ${OPENCV_INCLUDE_DIR}
    ${ONNXRUNTIME_INCLUDE_DIR}
)

# -----------------------------
# Link directories (Debug/Release)
# -----------------------------
target_link_directories(MuQt PRIVATE
    "$<$<CONFIG:Debug>:${MUPDF_LIB_DEBUG}>"
    "$<$<CONFIG:Release>:${MUPDF_LIB_RELEASE}>"

    "$<$<CONFIG:Debug>:${OPENCV_LIB_DEBUG}>"
    "$<$<CONFIG:Release>:${OPENCV_LIB_RELEASE}>"

    ${ONNXRUNTIME_LIB_DIR}
)

# -----------------------------
# Link libraries
# -----------------------------
target_link_libraries(MuQt PRIVATE
    Qt::Core
    Qt::Widgets
    Qt6::Concurrent

    # MuPDF
    libmupdf.lib
    libthirdparty.lib

    # OpenCV
    "$<$<CONFIG:Debug>:opencv_world4120d.lib>"
    "$<$<CONFIG:Release>:opencv_world4120.lib>"

    # ONNX Runtime
    onnxruntime.lib
)

# -----------------------------
# Copy runtime DLLs to output directory
# -----------------------------
# 定义 DLL 拷贝函数
function(copy_dlls target dll_list destination)
    foreach(dll ${dll_list})
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${dll}
                ${destination}
            COMMENT "Copying ${dll} to ${destination}"
        )
    endforeach()
endfunction()

# OpenCV DLLs
if(EXISTS "${OPENCV_BIN_DEBUG}/opencv_world4120d.dll")
    copy_dlls(MuQt
        "${OPENCV_BIN_DEBUG}/opencv_world4120d.dll"
        "$<TARGET_FILE_DIR:MuQt>"
    )
endif()

if(EXISTS "${OPENCV_BIN_RELEASE}/opencv_world4120.dll")
    copy_dlls(MuQt
        "${OPENCV_BIN_RELEASE}/opencv_world4120.dll"
        "$<TARGET_FILE_DIR:MuQt>"
    )
endif()

# ONNX Runtime DLLs (✅ 必须拷贝两个 DLL)
set(ONNX_DLLS
    "${ONNXRUNTIME_BIN_DIR}/onnxruntime.dll"
    "${ONNXRUNTIME_BIN_DIR}/onnxruntime_providers_shared.dll"
)

foreach(dll ${ONNX_DLLS})
    if(EXISTS ${dll})
        add_custom_command(TARGET MuQt POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${dll}
                $<TARGET_FILE_DIR:MuQt>
            COMMENT "Copying ${dll}"
        )
    else()
        message(WARNING "ONNX Runtime DLL not found: ${dll}")
    endif()
endforeach()

# -----------------------------
# Copy OCR models to output directory
# -----------------------------
set(OCR_MODELS_SRC "${CMAKE_SOURCE_DIR}/ocr/models")
set(OCR_MODELS_DST "$<TARGET_FILE_DIR:MuQt>/ocr/models")

if(EXISTS "${OCR_MODELS_SRC}")
    add_custom_command(TARGET MuQt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OCR_MODELS_DST}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${OCR_MODELS_SRC}
            ${OCR_MODELS_DST}
        COMMENT "Copying OCR models to output directory"
    )
else()
    message(WARNING "OCR models directory not found: ${OCR_MODELS_SRC}")
endif()

# -----------------------------
# Install section
# -----------------------------
include(GNUInstallDirs)

install(TARGETS MuQt
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# 安装 DLLs
install(FILES ${ONNX_DLLS}
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    OPTIONAL
)

# 安装 OCR 模型
install(DIRECTORY "${OCR_MODELS_SRC}"
    DESTINATION ${CMAKE_INSTALL_BINDIR}/ocr
    OPTIONAL
)

# -----------------------------
# Qt deployment
# -----------------------------
qt_generate_deploy_app_script(
    TARGET MuQt
    OUTPUT_SCRIPT deploy_script
)
install(SCRIPT ${deploy_script})
