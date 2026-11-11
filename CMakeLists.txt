cmake_minimum_required(VERSION 3.21)

project(NoteCanvas VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia)
qt_standard_project_setup(REQUIRES 6.8)

# The controls' style and the themes, taken unchanged from igitt
# (IgittStyle, see style/ and docs/THEMES.md there).
add_subdirectory(style)

qt_add_executable(notecanvas
    src/main.cpp
)

set_source_files_properties(qml/Theme.qml qml/Icons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(notecanvas
    URI NoteApp
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Sidebar.qml
        qml/CanvasView.qml
        qml/CanvasCard.qml
        qml/NoteBody.qml
        qml/VideoBlock.qml
        qml/SettingsDialog.qml
        qml/Toast.qml
        qml/Icons.qml
        qml/Icon.qml
        qml/IconButton.qml
        qml/SplitterHandle.qml
    SOURCES
        src/boardmodel.h src/boardmodel.cpp
        src/documentfilter.h src/documentfilter.cpp
        src/searchmodel.h src/searchmodel.cpp
        src/workspace.h src/workspace.cpp
        src/gridbackground.h src/gridbackground.cpp
        src/Fonts.h src/Fonts.cpp
    RESOURCES
        icons/check.svg
        icons/chevron-down.svg
        icons/copy.svg
        icons/download.svg
        icons/ellipsis.svg
        icons/film.svg
        icons/folder-open.svg
        icons/folder.svg
        icons/grip-vertical.svg
        icons/image.svg
        icons/maximize.svg
        icons/panel-left.svg
        icons/pause.svg
        icons/pencil.svg
        icons/play.svg
        icons/plus.svg
        icons/redo-2.svg
        icons/search.svg
        icons/settings.svg
        icons/sticky-note.svg
        icons/trash-2.svg
        icons/undo-2.svg
        icons/x.svg
        icons/zoom-in.svg
        icons/zoom-out.svg
        fonts/Inter-Bold.ttf
        fonts/Inter-Italic.ttf
        fonts/Inter-Medium.ttf
        fonts/Inter-Regular.ttf
        fonts/Inter-SemiBold.ttf
        fonts/JetBrainsMono-Bold.ttf
        fonts/JetBrainsMono-Italic.ttf
        fonts/JetBrainsMono-Regular.ttf
)

target_include_directories(notecanvas PRIVATE src)

target_link_libraries(notecanvas PRIVATE
    igitt_style
    igitt_styleplugin
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
)

include(GNUInstallDirs)
install(TARGETS notecanvas
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
