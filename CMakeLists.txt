cmake_minimum_required(VERSION 3.19)
project(JoPDF LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_PREFIX_PATH "C:/Qt/6.10.1/msvc2022_64")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Concurrent)

set(MUPDF_DIR "D:/Ext-Lib/mupdf-win-x64")

include_directories("${MUPDF_DIR}/include")
link_directories("${MUPDF_DIR}/lib/Debug")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/ui")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/session")
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
    manager/thumbnailbatchtask.cpp
    manager/thumbnailbatchtask.h
    manager/thumbnailmanagerv2.cpp
    manager/thumbnailmanagerv2.h
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
    model/thumbnailcache.cpp
    model/thumbnailcache.h
    model/thumbnailloadstrategy.cpp
    model/thumbnailloadstrategy.h
)

set(CORE_SOURCES
    core/mupdfrenderer.cpp
    core/mupdfrenderer.h
    core/threadsaferenderer.cpp
    core/threadsaferenderer.h
    core/mupdfrendererutil.h
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
    ui/navigationpanel.cpp
    ui/navigationpanel.h
    ui/pdfpagewidget.cpp
    ui/pdfpagewidget.h
    ui/pdfdocumenttab.cpp
    ui/pdfdocumenttab.h
)

set(SESSION_SOURCES
    session/pdfdocumentsession.cpp
    session/pdfdocumentsession.h
    session/pdfdocumentstate.cpp
    session/pdfdocumentstate.h
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
    ${SESSION_SOURCES}

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

if (WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(DEBUG_SUFFIX)
    if (MSVC AND CMAKE_BUILD_TYPE MATCHES "Debug")
        set(DEBUG_SUFFIX "d")
    endif ()
    set(QT_INSTALL_PATH "${CMAKE_PREFIX_PATH}")
    if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
        set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
            set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        endif ()
    endif ()
    if (EXISTS "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
    endif ()
    foreach (QT_LIB Core Gui Widgets)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/bin/Qt6${QT_LIB}${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach (QT_LIB)
endif ()

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
