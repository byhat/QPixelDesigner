cmake_minimum_required(VERSION 3.16)
project(QPixelDesigner VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Qml Quick QuickWidgets QuickControls2)

add_executable(QPixelDesigner
    src/codeeditor.h
    src/codeeditor.cpp
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/previewwindow.h
    src/previewwindow.cpp
    src/qmlhighlighter.h
    src/qmlhighlighter.cpp
    src/qmlpane.h
    src/qmlpane.cpp
    src/screencanvas.h
    src/screencanvas.cpp
    src/screenelement.h
    src/screenelement.cpp
)

target_include_directories(QPixelDesigner PRIVATE src)

target_link_libraries(QPixelDesigner PRIVATE
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::QuickControls2
)
