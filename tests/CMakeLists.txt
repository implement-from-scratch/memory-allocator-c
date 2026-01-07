cmake_minimum_required(VERSION 3.14)

# Enable testing
enable_testing()

# Find test frameworks
find_package(GTest)
find_package(Check)
find_package(CUnit)

# Unit Tests
file(GLOB UNIT_TEST_SOURCES unit/*.c unit/*.cpp)
foreach(test_source ${UNIT_TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} ${PROJECT_NAME}_lib GTest::GTest GTest::Main)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 60)
endforeach()

# Integration Tests
file(GLOB INTEGRATION_TEST_SOURCES integration/*.c integration/*.cpp)
foreach(test_source ${INTEGRATION_TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} ${PROJECT_NAME}_lib)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
endforeach()

# Coverage target
if(ENABLE_COVERAGE)
    include(CodeCoverage)
    setup_target_for_coverage_lcov(
        NAME coverage
        EXECUTABLE ctest
        DEPENDENCIES all_tests
        EXCLUDE "/usr/*" "*/tests/*" "*/build/*"
    )
endif()
