# Android POS target — guarded by MAHALI_ANDROID (see apps/CMakeLists.txt).
# Built only with a Qt for Android toolchain; the guard below still trips when
# the option is enabled on a desktop kit, so a mistaken configure fails here
# instead of at link time.

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

# Gradle package skeleton (AndroidManifest.xml and friends). It is filled in
# by a later phase, but the property has to point at it before the APK step
# runs, so it is set now while the target is being wired up.
set_target_properties(mahali-android PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")

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
