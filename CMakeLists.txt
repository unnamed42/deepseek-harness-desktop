cmake_minimum_required(VERSION 3.21)
project(deepseek-harness-desktop VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network WebEngineWidgets)
if(CMAKEW_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 6.5 REQUIRED COMPONENTS DBus)
endif()

set(QT_DEFAULT_MAJOR_VERSION 6 CACHE STRING "Qt version to use, defaults to 6")

set(QAPPLICATION_CLASS FreeStandingSingleApplication CACHE STRING "Inheritance class for SingleApplication")

FetchContent_Declare(
  SingleApplication
  GIT_REPOSITORY https://github.com/itay-grudev/SingleApplication
  GIT_TAG        master
  FIND_PACKAGE_ARGS NAMES SingleApplication
)
FetchContent_MakeAvailable(SingleApplication)

find_package(SingleApplication REQUIRED)

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
    $<$<PLATFORM_ID:Linux>:Qt6::DBus>

    SingleApplication::SingleApplication
)

install(TARGETS deepseek-harness-desktop
    RUNTIME DESTINATION lib/deepseek-harness-desktop/bin
)
