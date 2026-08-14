cmake_minimum_required(VERSION 3.21)
project(deepseek-harness-desktop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network WebEngineWidgets)

qt_standard_project_setup()

qt_add_executable(deepseek-harness-desktop
    src/main.cpp
    src/backendmanager.cpp
    src/backendmanager.h
    src/mainwindow.cpp
    src/mainwindow.h
    resources/resources.qrc
)

target_compile_definitions(deepseek-harness-desktop PRIVATE
    DSH_DESKTOP_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(deepseek-harness-desktop PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::WebEngineWidgets
)

install(TARGETS deepseek-harness-desktop
    RUNTIME DESTINATION lib/deepseek-harness-desktop/bin
)
