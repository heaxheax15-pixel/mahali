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
# No Qt Designer (.ui) forms are used; AUTOUIC is disabled because its
# `ui_<name>.h` pattern would misclassify plain headers like "ui_helpers.h".
set(CMAKE_AUTOUIC OFF)

option(MAHALI_ANDROID "Build the Android POS target (requires Qt for Android toolchain)" OFF)

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core Sql Network Widgets HttpServer Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Sql Network Widgets HttpServer Test)

# Translation catalogues. Consumed by apps/desktop/CMakeLists.txt, which
# compiles them with lrelease and embeds the resulting .qm under /i18n.
set(TS_FILES
    translations/mahali_ar.ts
    translations/mahali_fr.ts
    translations/mahali_en.ts)

# Qt6LinguistTools (and therefore qt_add_translations) is not shipped by every
# Qt6 package set, so resolve the lrelease tool directly and fall back to the
# classic add_custom_command pipeline in apps/desktop.
find_program(MAHALI_LRELEASE
    NAMES lrelease-qt6 lrelease6 lrelease
    HINTS
        ${QT6_INSTALL_PREFIX}/libexec
        ${QT6_INSTALL_PREFIX}/bin
        /usr/lib/qt6/bin
        /usr/lib/qt6/libexec
    DOC "Qt lrelease tool used to compile the .ts catalogues")
if(NOT MAHALI_LRELEASE)
    message(FATAL_ERROR
        "lrelease not found. Install qt6-l10n-tools (or the Qt LinguistTools "
        "development package) to build mahali-desktop.")
endif()
message(STATUS "mahali: lrelease = ${MAHALI_LRELEASE}")

add_subdirectory(core)
add_subdirectory(data)
add_subdirectory(network)
add_subdirectory(ui)
add_subdirectory(apps)

enable_testing()
add_subdirectory(tests)