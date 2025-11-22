cmake_minimum_required(VERSION 3.19)
project(JoPDF LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Concurrent)

set(MUPDF_DIR "D:/Ext-Lib/mupdf-win-x64")

include_directories("${MUPDF_DIR}/include")
link_directories("${MUPDF_DIR}/lib")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/ui")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/controller")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/model")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/core")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/util")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/tool")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/manager")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/handler")

set(HANDLER_SOURCES
    handler/pdfviewhandler.cpp
    handler/pdfviewhandler.h
    handler/pdfcontenthandler.cpp
    handler/pdfcontenthandler.h
    handler/pdfinteractionhandler.cpp
    handler/pdfinteractionhandler.h
)

set(MANAGER_SOURCES
    manager/thumbnailmanager.cpp
    manager/thumbnailmanager.h
    manager/linkmanager.cpp
    manager/linkmanager.h
    manager/outlinemanager.cpp
    manager/outlinemanager.h
    manager/searchmanager.cpp
    manager/searchmanager.h
)

set(TOOL_SOURCES
    tool/textselector.cpp
    tool/textselector.h
    tool/outlineeditor.cpp
    tool/outlineeditor.h
)

set(MODEL_SOURCES
    model/outlineitem.cpp
    model/outlineitem.h
)

set(CORE_SOURCES
    core/mupdfrenderer.cpp
    core/mupdfrenderer.h
)

set(UI_SOURCES
    ui/thumbnailwidget.cpp
    ui/thumbnailwidget.h
    ui/outlinewidget.cpp
    ui/outlinewidget.h
    ui/outlinedialog.cpp
    ui/outlinedialog.h
    ui/searchwidget.cpp
    ui/searchwidget.h
)

set(CONTROLLER_SOURCES
    controller/pdfdocumentsession.cpp
    controller/pdfdocumentsession.h
)

set(UTIL_SOURCES
    util/datastructure.h
    util/appconfig.cpp
    util/appconfig.h
)

# Source files
set(SOURCES
    main.cpp

    mainwindow.cpp
    mainwindow.h
    pdfpagewidget.cpp
    pdfpagewidget.h
    navigationpanel.cpp
    navigationpanel.h


    pagecachemanager.cpp
    pagecachemanager.h
    textcachemanager.cpp
    textcachemanager.h


    ${UI_SOURCES}
    ${TOOL_SOURCES}
    ${CORE_SOURCES}
    ${HANDLER_SOURCES}
    ${MODEL_SOURCES}
    ${UTIL_SOURCES}
    ${MANAGER_SOURCES}
    ${CONTROLLER_SOURCES}

    icons.qrc
)

qt_standard_project_setup()

qt_add_executable(JoPDF
    WIN32 MACOSX_BUNDLE
    ${SOURCES}
)

target_link_libraries(JoPDF
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt6::Concurrent
        libmupdf.lib
        libthirdparty.lib
)

include(GNUInstallDirs)

install(TARGETS JoPDF
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET JoPDF
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
