if(ANDROID)
    # Android kit: only the phone app. The desktop target would build
    # against the phone's Qt and produce a binary that is laid out for a
    # mouse, with menus and toolbars that no thumb can hit.
    add_subdirectory(android)
else()
    add_subdirectory(desktop)

    option(MAHALI_TOOLS "Build developer tools (screenshot renderer)" OFF)
    if(MAHALI_TOOLS)
        add_subdirectory(screenshot)
    endif()
endif()
