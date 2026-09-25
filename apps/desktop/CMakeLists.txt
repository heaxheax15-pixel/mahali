add_executable(mahali-desktop main.cpp)

target_link_libraries(mahali-desktop
    PRIVATE
        mahali-ui
        Qt${QT_VERSION_MAJOR}::Widgets)

target_compile_definitions(mahali-desktop
    PRIVATE
        MAHALI_VERSION="${PROJECT_VERSION}")