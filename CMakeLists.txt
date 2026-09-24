cmake_minimum_required(VERSION 3.16)

project(mahali
    VERSION 0.1.0
    DESCRIPTION "POS + Inventory + Accounting + Sync (offline-first grocery)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(MAHALI_ANDROID "Build the Android POS target (requires Qt for Android toolchain)" OFF)

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core Sql Network Widgets HttpServer Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Sql Network Widgets HttpServer Test)

add_subdirectory(core)
add_subdirectory(data)
add_subdirectory(network)
add_subdirectory(ui)
add_subdirectory(apps)

enable_testing()
add_subdirectory(tests)