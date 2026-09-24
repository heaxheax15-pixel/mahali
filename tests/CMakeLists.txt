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

add_executable(tst_data tst_data.cpp)
target_link_libraries(tst_data
    PRIVATE
        mahali-data
        Qt${QT_VERSION_MAJOR}::Test)

add_test(NAME data COMMAND tst_data)

add_executable(tst_finance tst_finance.cpp)
target_link_libraries(tst_finance
    PRIVATE
        mahali-core
        Qt${QT_VERSION_MAJOR}::Test)

add_test(NAME finance COMMAND tst_finance)