add_subdirectory(desktop)

option(MAHALI_TOOLS "Build developer tools (screenshot renderer)" OFF)
if(MAHALI_TOOLS)
    add_subdirectory(screenshot)
endif()

if(MAHALI_ANDROID)
    add_subdirectory(android)
endif()