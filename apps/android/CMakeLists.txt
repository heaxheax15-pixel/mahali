# Android POS target — included by apps/CMakeLists.txt only when the CMake kit
# sets ANDROID (the Qt-for-Android toolchain always does). The guard below still
# trips if the directory is ever reached on a desktop kit, so a mistaken
# configure fails here instead of at link time.

if(NOT ANDROID)
    message(FATAL_ERROR "mahali-android requires an Android toolchain (ANDROID).")
endif()

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)

qt_add_executable(mahali-android
    main.cpp
    android_backend.cpp)

# The QML side of the shell, registered as the module "Mahali". QTP0001 is
# not set anywhere in this project, so the resource prefix is "/" and the
# module lands at qrc:/Mahali/ — main.cpp loads qrc:/Mahali/qml/main.qml.
qt_add_qml_module(mahali-android
    URI Mahali
    VERSION 1.0
    QML_FILES
        qml/main.qml
        qml/ProductListPage.qml)

# No custom Android package source dir yet. Qt for Android will generate
# a default AndroidManifest.xml internally, so we let it fall back to its
# built-in defaults instead of pointing at a non-existent directory.


# mahali-core / mahali-data are the same libraries the desktop build links, so
# the phone reads the identical schema and repositories. Qt6::Sql comes with
# mahali-data but is named here as well because Database talks to it directly.
target_link_libraries(mahali-android
    PRIVATE
        mahali-core
        mahali-data
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Sql)
