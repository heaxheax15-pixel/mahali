find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

add_executable(tst_smoke tst_smoke.cpp)
target_link_libraries(tst_smoke
    PRIVATE
        mahali-core
        mahali-data
        mahali-network
        mahali-ui
        Qt${QT_VERSION_MAJOR}::Test)

add_test(NAME smoke COMMAND tst_smoke)