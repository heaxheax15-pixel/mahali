add_executable(mahali-screenshot main.cpp)

add_dependencies(mahali-screenshot mahali_translations)

target_sources(mahali-screenshot PRIVATE
    ${CMAKE_BINARY_DIR}/apps/desktop/translations/mahali_translations.qrc)

target_link_libraries(mahali-screenshot
    PRIVATE
        mahali-ui
        Qt${QT_VERSION_MAJOR}::Widgets)