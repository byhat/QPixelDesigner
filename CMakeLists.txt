cmake_minimum_required(VERSION 3.16)
project(PixelUiDesigner VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Qml Quick QuickWidgets)

add_executable(pixeldesigner
    main.cpp
    mainwindow.h
    mainwindow.cpp
    previewwindow.h
    previewwindow.cpp
    qmlhighlighter.h
    qmlhighlighter.cpp
    qmlpane.h
    qmlpane.cpp
    screencanvas.h
    screencanvas.cpp
    screenelement.h
    screenelement.cpp
)

target_link_libraries(pixeldesigner PRIVATE
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickWidgets
)
