add_executable(mahali-screenshot main.cpp)

target_link_libraries(mahali-screenshot
    PRIVATE
        mahali-ui
        Qt${QT_VERSION_MAJOR}::Widgets)