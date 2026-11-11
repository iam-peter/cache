cmake_minimum_required(VERSION 3.21)

project(Cache VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia Network Svg)
qt_standard_project_setup(REQUIRES 6.8)

# Lay QML modules out by URI so qmllint and qmlls find them (as igitt does)
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)

# The controls' style and the themes, taken unchanged from igitt
# (IgittStyle, see style/ and docs/THEMES.md there).
add_subdirectory(style)

qt_add_executable(cache WIN32 MACOSX_BUNDLE
    src/main.cpp
)
target_compile_definitions(cache PRIVATE PROJECT_VERSION_STRING="${PROJECT_VERSION}")
# The icon the file manager, taskbar and Dock show
set_target_properties(cache PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME Cache
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.iam-peter.cache
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE cache.icns
)
if(WIN32)
    target_sources(cache PRIVATE logo/cache.rc)
elseif(APPLE)
    target_sources(cache PRIVATE logo/cache.icns)
    set_source_files_properties(logo/cache.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

file(GLOB icon_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS icons/*.svg)
file(GLOB template_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS templates/*.json)

set_source_files_properties(qml/Theme.qml qml/Icons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(cache
    URI Cache
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Sidebar.qml
        qml/CanvasView.qml
        qml/CanvasCard.qml
        qml/NoteBody.qml
        qml/VideoBlock.qml
        qml/CodeBlock.qml
        qml/EmbedBlock.qml
        qml/FileBlock.qml
        qml/ShapeBody.qml
        qml/TextBody.qml
        qml/SectionBody.qml
        qml/StrokeBody.qml
        qml/Connector.qml
        qml/ToolPalette.qml
        qml/Minimap.qml
        qml/SelectionBar.qml
        qml/LinkBar.qml
        qml/LinkEnds.qml
        qml/TemplateDialog.qml
        qml/HistoryDialog.qml
        qml/SettingsDialog.qml
        qml/Toast.qml
        qml/Icons.qml
        qml/Icon.qml
        qml/IconButton.qml
        qml/SplitterHandle.qml
    SOURCES
        src/boardmodel.h src/boardmodel.cpp src/boardops.cpp src/importer.cpp
        src/markdown.h src/markdown.cpp
        src/highlight.h src/highlight.cpp
        src/documentfilter.h src/documentfilter.cpp
        src/searchmodel.h src/searchmodel.cpp
        src/fuzzy.h src/fuzzy.cpp
        src/history.h src/history.cpp
        src/workspace.h src/workspace.cpp
        src/gridbackground.h src/gridbackground.cpp
        src/Fonts.h src/Fonts.cpp
    RESOURCES
        ${icon_files}
        ${template_files}
        logo/cache.png
        fonts/Inter-Bold.ttf
        fonts/Inter-Italic.ttf
        fonts/Inter-Medium.ttf
        fonts/Inter-Regular.ttf
        fonts/Inter-SemiBold.ttf
        fonts/JetBrainsMono-Bold.ttf
        fonts/JetBrainsMono-Italic.ttf
        fonts/JetBrainsMono-Regular.ttf
)

target_include_directories(cache PRIVATE src)

target_link_libraries(cache PRIVATE
    igitt_style
    igitt_styleplugin
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    Qt6::Network
    # Not called directly: linking it makes the deploy tools bring the SVG
    # image plugin the icons need
    Qt6::Svg
)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(tst_fuzzy tests/tst_fuzzy.cpp src/fuzzy.h src/fuzzy.cpp)
    target_include_directories(tst_fuzzy PRIVATE src)
    target_link_libraries(tst_fuzzy PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tst_fuzzy COMMAND tst_fuzzy)

    qt_add_executable(tst_markdown tests/tst_markdown.cpp
        src/markdown.h src/markdown.cpp src/highlight.h src/highlight.cpp)
    target_include_directories(tst_markdown PRIVATE src)
    target_link_libraries(tst_markdown PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME tst_markdown COMMAND tst_markdown)

    # The board model as the app uses it, on an offscreen platform
    qt_add_executable(tst_board tests/tst_board.cpp
        src/boardmodel.h src/boardmodel.cpp src/boardops.cpp src/importer.cpp
        src/markdown.h src/markdown.cpp src/highlight.h src/highlight.cpp)
    target_include_directories(tst_board PRIVATE src)
    target_link_libraries(tst_board PRIVATE Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test)
    qt_add_resources(tst_board "templates" PREFIX /qt/qml/Cache FILES ${template_files})
    add_test(NAME tst_board COMMAND tst_board -platform offscreen)

    qt_add_executable(tst_history tests/tst_history.cpp src/history.h src/history.cpp)
    target_include_directories(tst_history PRIVATE src)
    target_link_libraries(tst_history PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
    add_test(NAME tst_history COMMAND tst_history)

    # Console programs: qt_standard_project_setup makes GUI programs on
    # Windows, which print nothing, so a failing test would leave an empty log
    set_target_properties(tst_fuzzy tst_markdown tst_board tst_history PROPERTIES
        WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
endif()

include(GNUInstallDirs)
install(TARGETS cache
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(UNIX AND NOT APPLE)
    # Desktop environments find the icon by the desktop file's Icon= name
    install(FILES logo/cache.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES logo/cache.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
