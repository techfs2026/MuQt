cmake_minimum_required(VERSION 3.19)
project(OwlPDF LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# macOS 上需要 Objective-C++ 来编译 .mm（Vision 桥接）
if(APPLE)
    enable_language(OBJCXX)
endif()

# ============================================================
# 平台相关路径配置
# ============================================================

if(WIN32)
    # ---------- Windows ----------
    set(CMAKE_PREFIX_PATH "D:/Qt6/6.8.3/msvc2022_64")
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/deploy")

    set(MUPDF_DIR         "D:/Ext-Lib/mupdf-win-x64")
    set(OPENCV_DIR        "D:/Ext-Lib/opencv-win-x64")
    set(ONNXRUNTIME_DIR   "D:/Ext-Lib/onnxruntime-win-x64")

    set(MUPDF_INCLUDE_DIR      "${MUPDF_DIR}/include")
    set(MUPDF_LIB_DEBUG        "${MUPDF_DIR}/lib/Debug")
    set(MUPDF_LIB_RELEASE      "${MUPDF_DIR}/lib/Release")

    set(OPENCV_INCLUDE_DIR     "${OPENCV_DIR}/include")
    set(OPENCV_LIB_DEBUG       "${OPENCV_DIR}/lib/Debug")
    set(OPENCV_LIB_RELEASE     "${OPENCV_DIR}/lib/Release")
    set(OPENCV_BIN_DEBUG       "${OPENCV_DIR}/bin/Debug")
    set(OPENCV_BIN_RELEASE     "${OPENCV_DIR}/bin/Release")

    set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_DIR}/include")
    set(ONNXRUNTIME_LIB_DIR     "${ONNXRUNTIME_DIR}/lib")
    set(ONNXRUNTIME_BIN_DIR     "${ONNXRUNTIME_DIR}/lib")

elseif(APPLE)
    # ---------- macOS ----------
    # macOS 使用系统 Vision 框架做 OCR，不需要 onnxruntime / RapidOCR 模型。
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT HOMEBREW_PREFIX)
        set(HOMEBREW_PREFIX "/opt/homebrew")  # Apple Silicon 默认
    endif()

    # Qt6
    set(CMAKE_PREFIX_PATH "/Users/fs2026/Qt/6.11.1/macos")
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/deploy")

    set(MUPDF_DIR         "${HOMEBREW_PREFIX}/opt/mupdf")
    set(MUPDF_INCLUDE_DIR "${MUPDF_DIR}/include")
    set(MUPDF_LIB_DIR     "${MUPDF_DIR}/lib")

    # OpenCV：brew install opencv（仍需保留：cv::Point2f 类型 + 护眼功能）
    set(OPENCV_DIR        "${HOMEBREW_PREFIX}/opt/opencv")
    set(OPENCV_INCLUDE_DIR "${OPENCV_DIR}/include/opencv4")
    set(OPENCV_LIB_DIR    "${OPENCV_DIR}/lib")

    # 注意：macOS 不再使用 onnxruntime
endif()

# ============================================================
# Qt6
# ============================================================

if(WIN32)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Concurrent Svg LinguistTools)
elseif(APPLE)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Core Widgets Concurrent Svg LinguistTools)
endif()

# ============================================================
# 应用图标（平台专属资源）
# ============================================================

if(WIN32)
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
elseif(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE "app.icns")
    set(APP_ICON_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/app.icns")
    set_source_files_properties(${APP_ICON_MACOS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
endif()

# ============================================================
# 源文件收集
# ============================================================

file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc"
)

list(FILTER PROJECT_SOURCES EXCLUDE REGEX "${CMAKE_CURRENT_BINARY_DIR}")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX ".*_autogen.*")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX ".*/CMakeFiles/.*")
# 源码树内任何构建目录（build/、build-release/ 等）及 Qt 生成文件都不算源文件，
# 否则同时存在多个构建目录时，旧目录里的 qrc_*.cpp 会被重复编入导致 duplicate symbol
list(FILTER PROJECT_SOURCES EXCLUDE REGEX ".*/build[^/]*/.*")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX ".*/\\.qt/.*")

# 平台相关源文件过滤：
if(APPLE)
    # macOS 用 Vision，整个 RapidOCR 实现（含 onnx 依赖）全部剔除
    list(FILTER PROJECT_SOURCES EXCLUDE REGEX ".*/rapidocr-cpp/.*")
    list(FILTER PROJECT_SOURCES EXCLUDE REGEX ".*rapidocrengine\\.cpp$")
    list(FILTER PROJECT_SOURCES EXCLUDE REGEX ".*rapidocrengine\\.h$")
else()
    # Windows/Linux 不编译 Vision 引擎
    list(FILTER PROJECT_SOURCES EXCLUDE REGEX ".*visionocrengine\\.mm$")
    list(FILTER PROJECT_SOURCES EXCLUDE REGEX ".*visionocrengine\\.h$")
endif()

# ============================================================
# 翻译文件
# ============================================================

set(TS_FILES
    translations/owlpdf_zh_CN.ts
    translations/owlpdf_en_US.ts
)

qt_standard_project_setup()

# ============================================================
# 可执行目标
# ============================================================

qt_add_executable(OwlPDF
    WIN32 MACOSX_BUNDLE
    ${PROJECT_SOURCES}
    $<$<BOOL:${WIN32}>:${APP_ICON_RESOURCE_WINDOWS}>
    $<$<BOOL:${APPLE}>:${APP_ICON_MACOS}>
)

qt_add_translations(OwlPDF
    TS_FILES ${TS_FILES}
    RESOURCE_PREFIX "/translations"
)

set(QT_TRANSLATIONS_DIR "${CMAKE_PREFIX_PATH}/translations")
set(QT_STANDARD_TRANSLATION_FILES
    qtbase_zh_CN.qm
    qt_zh_CN.qm
)

# ============================================================
# macOS 应用包 Info.plist
#   使用自定义 Info.plist，声明 OwlPDF 可处理 PDF 文件
#   （CFBundleDocumentTypes），使其能被设为默认 PDF 程序。
#   设置该属性后 Qt 不会再生成默认 plist。
# ============================================================

if(APPLE)
    set_target_properties(OwlPDF PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
        BUILD_RPATH "@executable_path/../Frameworks;${CMAKE_PREFIX_PATH}/lib;${MUPDF_LIB_DIR};${OPENCV_LIB_DIR};${HOMEBREW_PREFIX}/lib"
        INSTALL_RPATH "@executable_path/../Frameworks"
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
endif()

# ============================================================
# 头文件目录
# ============================================================

target_include_directories(OwlPDF PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ocr
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/theme
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
)

# onnxruntime 头文件目录仅非 APPLE 平台需要
if(NOT APPLE)
    target_include_directories(OwlPDF PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
endif()

# ============================================================
# 链接目录
# ============================================================

if(WIN32)
    target_link_directories(OwlPDF PRIVATE
        "$<$<CONFIG:Debug>:${MUPDF_LIB_DEBUG}>"
        "$<$<CONFIG:Release>:${MUPDF_LIB_RELEASE}>"
        "$<$<CONFIG:Debug>:${OPENCV_LIB_DEBUG}>"
        "$<$<CONFIG:Release>:${OPENCV_LIB_RELEASE}>"
        ${ONNXRUNTIME_LIB_DIR}
    )
elseif(APPLE)
    target_link_directories(OwlPDF PRIVATE
        ${MUPDF_LIB_DIR}
        ${OPENCV_LIB_DIR}
    )
endif()

# ============================================================
# 链接库
# ============================================================

target_link_libraries(OwlPDF PRIVATE
    Qt::Core
    Qt::Widgets
    Qt6::Concurrent
    Qt6::Svg
)

if(WIN32)
    target_link_libraries(OwlPDF PRIVATE
        libmupdf.lib
        libthirdparty.lib
        "$<$<CONFIG:Debug>:opencv_world4120d.lib>"
        "$<$<CONFIG:Release>:opencv_world4120.lib>"
        onnxruntime.lib
    )
elseif(APPLE)
    # 仅链接实际用到的模块（core + imgproc）。链全量 ${OpenCV_LIBS} 会把 viz→VTK、
    # dnn、gapi 等一并拖入，打包时 Frameworks 暴涨到 245MB+ 且引入 57 个 VTK 库。
    find_package(OpenCV REQUIRED COMPONENTS core imgproc)

    # mupdf 编译时依赖的系统库
    find_package(ZLIB REQUIRED)
    find_package(JPEG REQUIRED)
    find_package(Freetype REQUIRED)

    find_library(BROTLI_ENC_LIB    brotlienc    HINTS "${HOMEBREW_PREFIX}/lib" REQUIRED)
    find_library(BROTLI_DEC_LIB    brotlidec    HINTS "${HOMEBREW_PREFIX}/lib" REQUIRED)
    find_library(BROTLI_COMMON_LIB brotlicommon HINTS "${HOMEBREW_PREFIX}/lib" REQUIRED)
    find_library(JBIG2_LIB         jbig2dec     HINTS "${HOMEBREW_PREFIX}/lib" REQUIRED)
    find_library(OPENJP2_LIB       openjp2      HINTS "${HOMEBREW_PREFIX}/lib" REQUIRED)
    find_library(GUMBO_LIB         gumbo        HINTS "${HOMEBREW_PREFIX}/lib" REQUIRED)
    find_library(MUPDF_THIRD_LIB   mupdf-third  HINTS "${MUPDF_LIB_DIR}" REQUIRED)

    target_link_libraries(OwlPDF PRIVATE
        ${OpenCV_LIBS}

        mupdf
        ${MUPDF_THIRD_LIB}

        ${BROTLI_ENC_LIB}
        ${BROTLI_DEC_LIB}
        ${BROTLI_COMMON_LIB}
        ${JBIG2_LIB}
        ${OPENJP2_LIB}
        ${GUMBO_LIB}
        Freetype::Freetype
        JPEG::JPEG
        ZLIB::ZLIB

        # macOS 系统框架
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework CoreText"
        "-framework Foundation"
        "-framework ImageIO"
        "-framework Vision"        # OCR 引擎
    )
endif()

# ============================================================
# POST_BUILD：拷贝运行时动态库
# ============================================================

if(WIN32)
    add_custom_command(TARGET OwlPDF POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying OpenCV DLL for $<CONFIG>..."
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${OPENCV_BIN_DEBUG}/opencv_world4120d.dll,${OPENCV_BIN_RELEASE}/opencv_world4120.dll>"
            "$<TARGET_FILE_DIR:OwlPDF>/"
        VERBATIM
    )

    add_custom_command(TARGET OwlPDF POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying ONNX Runtime DLLs..."
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_BIN_DIR}/onnxruntime.dll"
            "$<TARGET_FILE_DIR:OwlPDF>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_BIN_DIR}/onnxruntime_providers_shared.dll"
            "$<TARGET_FILE_DIR:OwlPDF>/"
        VERBATIM
    )
# macOS 不需要拷贝 onnxruntime.dylib（已不使用）
endif()

if(WIN32)
    set(QT_STANDARD_TRANSLATIONS_DST "$<TARGET_FILE_DIR:OwlPDF>/translations")
elseif(APPLE)
    set(QT_STANDARD_TRANSLATIONS_DST "$<TARGET_BUNDLE_CONTENT_DIR:OwlPDF>/Resources/translations")
endif()

if(QT_STANDARD_TRANSLATIONS_DST)
    foreach(QT_TRANSLATION_FILE IN LISTS QT_STANDARD_TRANSLATION_FILES)
        if(EXISTS "${QT_TRANSLATIONS_DIR}/${QT_TRANSLATION_FILE}")
            add_custom_command(TARGET OwlPDF POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${QT_STANDARD_TRANSLATIONS_DST}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${QT_TRANSLATIONS_DIR}/${QT_TRANSLATION_FILE}"
                    "${QT_STANDARD_TRANSLATIONS_DST}/${QT_TRANSLATION_FILE}"
                COMMENT "Copying Qt standard translation ${QT_TRANSLATION_FILE}"
                VERBATIM
            )
        else()
            message(WARNING "Qt standard translation not found: ${QT_TRANSLATIONS_DIR}/${QT_TRANSLATION_FILE}")
        endif()
    endforeach()
endif()

# ============================================================
# OCR 模型 & Jieba 词典
#   - Windows/Linux：需要 OCR 模型（RapidOCR/onnx）
#   - macOS：用 Vision，不需要 OCR 模型，但仍需 Jieba 词典（中文分词）
# ============================================================

set(OCR_MODELS_SRC "${CMAKE_SOURCE_DIR}/ocr/models")
set(JIEBA_DICT_SRC "${CMAKE_SOURCE_DIR}/ocr/dict")

if(WIN32)
    set(OCR_MODELS_DST "$<TARGET_FILE_DIR:OwlPDF>/ocr/models")
    set(JIEBA_DICT_DST "$<TARGET_FILE_DIR:OwlPDF>/ocr/dict")
elseif(APPLE)
    set(JIEBA_DICT_DST "$<TARGET_BUNDLE_CONTENT_DIR:OwlPDF>/Resources/ocr/dict")
endif()

# OCR 模型：仅非 APPLE 拷贝
if(NOT APPLE)
    if(EXISTS "${OCR_MODELS_SRC}")
        add_custom_command(TARGET OwlPDF POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${OCR_MODELS_DST}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${OCR_MODELS_SRC}" "${OCR_MODELS_DST}"
            COMMENT "Copying OCR models"
        )
    else()
        message(WARNING "OCR models directory not found: ${OCR_MODELS_SRC}")
    endif()
endif()

# Jieba 词典：所有平台都需要
if(EXISTS "${JIEBA_DICT_SRC}")
    add_custom_command(TARGET OwlPDF POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${JIEBA_DICT_DST}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${JIEBA_DICT_SRC}" "${JIEBA_DICT_DST}"
        COMMENT "Copying Jieba dict"
    )
else()
    message(WARNING "Jieba dict directory not found: ${JIEBA_DICT_SRC}")
endif()

# ============================================================
# 部署工具（windeployqt / macdeployqt）
# ============================================================

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${CMAKE_PREFIX_PATH}/bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET OwlPDF POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Running windeployqt..."
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --no-compiler-runtime
                "$<TARGET_FILE:OwlPDF>"
            VERBATIM
        )
        message(STATUS "Found windeployqt: ${WINDEPLOYQT_EXECUTABLE}")
    else()
        message(WARNING "windeployqt not found. Please run it manually after build.")
    endif()

elseif(APPLE)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${CMAKE_PREFIX_PATH}/bin")
    if(MACDEPLOYQT_EXECUTABLE)
        add_custom_target(deploy
            COMMAND ${CMAKE_COMMAND} -E echo "Running macdeployqt..."
            COMMAND "${MACDEPLOYQT_EXECUTABLE}"
                "$<TARGET_BUNDLE_DIR:OwlPDF>"
            DEPENDS OwlPDF
            VERBATIM
        )
        message(STATUS "Found macdeployqt: ${MACDEPLOYQT_EXECUTABLE}. Run 'cmake --build . --target deploy' to package.")
    else()
        message(WARNING "macdeployqt not found.")
    endif()
endif()

# ============================================================
# 安装规则
# ============================================================

include(GNUInstallDirs)

install(TARGETS OwlPDF
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(NOT APPLE)
    install(DIRECTORY "${OCR_MODELS_SRC}"
        DESTINATION ${CMAKE_INSTALL_BINDIR}/ocr
        OPTIONAL
    )
endif()

qt_generate_deploy_app_script(
    TARGET OwlPDF
    OUTPUT_SCRIPT deploy_script
)
install(SCRIPT ${deploy_script})
