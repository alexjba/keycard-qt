# Qt Test framework
find_package(Qt6 REQUIRED COMPONENTS Test)

# Enable testing
enable_testing()

# Test helper - create test executable
function(add_keycard_test test_name)
    add_executable(${test_name} ${test_name}.cpp)
    
    target_link_libraries(${test_name}
        PRIVATE
            keycard-qt
            Qt6::Test
    )
    
    # Enable test helpers for bypassing crypto in unit tests
    target_compile_definitions(${test_name} PRIVATE KEYCARD_ENABLE_TEST_HELPERS)
    
    # Add as test
    add_test(NAME ${test_name} COMMAND ${test_name})
    
    # Set working directory
    set_tests_properties(${test_name} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endfunction()

# Test files
add_keycard_test(test_apdu_command)
add_keycard_test(test_apdu_response)
add_keycard_test(test_apdu_utils)
add_keycard_test(test_secure_channel)
add_keycard_test(test_command_set)
add_keycard_test(test_types)

# Extended test suites for higher coverage
add_keycard_test(test_command_set_extended)
add_keycard_test(test_secure_channel_extended)
add_keycard_test(test_types_extended)
add_keycard_test(test_keycard_channel)
add_keycard_test(test_command_set_new)
add_keycard_test(test_pbkdf2)
add_keycard_test(test_init_pair)

message(STATUS "Unit tests configured - run with: ctest --verbose")

