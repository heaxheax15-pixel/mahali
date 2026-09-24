# Android POS target — guarded by MAHALI_ANDROID. Built only when a Qt for
# Android toolchain is present (stage 6). Kept as scaffold here.

if(NOT ANDROID)
    message(FATAL_ERROR "mahali-android requires an Android toolchain (ANDROID).")
endif()

add_executable(mahali-android main.cpp)

target_link_libraries(mahali-android
    PRIVATE
        mahali-core
        mahali-data
        mahali-network)