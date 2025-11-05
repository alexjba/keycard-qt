# Examples for keycard-qt

find_package(Qt6 REQUIRED COMPONENTS Core)

# Helper function to create examples
function(add_keycard_example example_name)
    add_executable(${example_name} ${example_name}.cpp)
    
    target_link_libraries(${example_name}
        PRIVATE
            keycard-qt
            Qt6::Core
    )
    
    # Set RPATH for finding libraries at build time
    if(APPLE)
        set_target_properties(${example_name} PROPERTIES
            BUILD_WITH_INSTALL_RPATH FALSE
            BUILD_RPATH "${CMAKE_BINARY_DIR};${CMAKE_PREFIX_PATH}/lib"
            INSTALL_RPATH "@executable_path/../lib"
        )
    elseif(UNIX)
        set_target_properties(${example_name} PROPERTIES
            BUILD_RPATH "${CMAKE_BINARY_DIR}"
            INSTALL_RPATH "$ORIGIN/../lib"
        )
    endif()
endfunction()

# Simple detection example
add_keycard_example(simple_detection)

# Full test application
add_keycard_example(full_test)

# Advanced test application
add_keycard_example(advanced_test)

# Pair test application
add_keycard_example(pair_test)

# Factory reset test application
add_keycard_example(factory_reset_test)

# Full integration test application
add_keycard_example(full_integration_test)

message(STATUS "Examples configured - build with: make simple_detection full_test advanced_test pair_test factory_reset_test full_integration_test")

