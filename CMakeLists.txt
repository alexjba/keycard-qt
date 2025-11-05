cmake_minimum_required(VERSION 3.16)
project(keycard-qt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt modules
# Core is always required
# Nfc is required for iOS and Android (Desktop uses PC/SC)
find_package(Qt6 REQUIRED COMPONENTS Core)

if(IOS OR ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS Nfc)
    if(IOS)
        message(STATUS "iOS build: Using Qt NFC")
    else()
        message(STATUS "Android build: Using Qt NFC (with JNI fix for onNewIntent)")
    endif()
else()
    message(STATUS "Desktop build: Using PC/SC (no Qt NFC needed)")
endif()


# OpenSSL for secp256k1 ECDH
# For Android, we use manually provided paths instead of find_package
if(ANDROID AND OPENSSL_CRYPTO_LIBRARY AND OPENSSL_BUILD_INCLUDE_DIR AND OPENSSL_SOURCE_INCLUDE_DIR)
    set(OpenSSL_FOUND TRUE)
    message(STATUS "Using manually provided OpenSSL for Android")
    message(STATUS "  Crypto library: ${OPENSSL_CRYPTO_LIBRARY}")
    message(STATUS "  Build include dir: ${OPENSSL_BUILD_INCLUDE_DIR}")
    message(STATUS "  Source include dir: ${OPENSSL_SOURCE_INCLUDE_DIR}")
else()
    find_package(OpenSSL)
    if(OpenSSL_FOUND)
        message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")
    else()
        message(WARNING "OpenSSL not found. Building without secp256k1 ECDH support.")
    endif()
endif()

# Library sources
set(KEYCARD_QT_SOURCES
    # APDU layer
    src/apdu/command.cpp
    src/apdu/response.cpp
    src/apdu/utils.cpp
    
    # Channel (main factory)
    src/channel/keycard_channel.cpp
    
    # Crypto
    src/crypto/secure_channel.cpp
    
    # Types
    src/types/application_info.cpp
    src/types/application_status.cpp
    src/types/pairing_info.cpp
    src/types/exported_key.cpp
    src/types/secrets.cpp
    
    # Command Set
    src/command_set.cpp
)

# ============================================================================
# Backend Selection (Plugin Architecture)
# ============================================================================
# Select appropriate backend based on platform:
#   - Desktop (macOS/Linux/Windows): PC/SC backend
#   - Mobile (iOS/Android): Choose backend
# ============================================================================
if(IOS)
    message(STATUS "Backend: Qt NFC (iOS)")
    list(APPEND KEYCARD_QT_SOURCES
        src/channel/backends/keycard_channel_qt_nfc.cpp
    )
elseif(ANDROID)
    # Option to use Android NFC backend instead of Qt NFC
    option(USE_ANDROID_NFC_BACKEND "Use direct Android NFC backend instead of Qt NFC" ON)
    if(USE_ANDROID_NFC_BACKEND)
        message(STATUS "Backend: Android NFC (direct JNI)")
        add_definitions(-DUSE_ANDROID_NFC_BACKEND)
        list(APPEND KEYCARD_QT_SOURCES
            src/channel/backends/keycard_channel_android_nfc.cpp
        )
        
        # ============================================================================
        # Android NFC Java Classes - Build JAR
        # ============================================================================
        # Build the Android NFC helper classes into a JAR file
        # This makes the library self-contained - consumers don't need to copy Java files
        # Only needed when using Android NFC backend (requires Java helper classes)
        # ============================================================================
        
        include(cmake/AndroidNfcJar.cmake)
        
    else()
        message(STATUS "Backend: Qt NFC (Android)")
        list(APPEND KEYCARD_QT_SOURCES
            src/channel/backends/keycard_channel_qt_nfc.cpp
        )
    endif()
    
    # Add ENABLE_QT_NFC_ANDROID_WORKAROUNDS as a compiler definition
    if(ENABLE_QT_NFC_ANDROID_WORKAROUNDS)
        add_definitions(-DENABLE_QT_NFC_ANDROID_WORKAROUNDS=1)
        message(STATUS "Compiler flag: -DENABLE_QT_NFC_ANDROID_WORKAROUNDS=1")
    else()
        add_definitions(-DENABLE_QT_NFC_ANDROID_WORKAROUNDS=0)
        message(STATUS "Compiler flag: -DENABLE_QT_NFC_ANDROID_WORKAROUNDS=0")
    endif()
else()
    message(STATUS "Backend: PC/SC (Desktop)")
    list(APPEND KEYCARD_QT_SOURCES
        src/channel/backends/keycard_channel_pcsc.cpp
    )
endif()

# ============================================================================
# Qt NFC Android Workarounds Configuration
# ============================================================================
# Enable workarounds for Qt 6.9.x NFC bugs on Android
# Set to OFF when testing newer Qt versions or when Qt fixes the bugs
#
# Bugs being worked around:
#   1. QtNative.onNewIntent() JNI method never registered
#   2. QNearFieldManager::startTargetDetection() doesn't call startDiscovery()
#   3. targetDetected signal doesn't fire
#
# See: QT_NFC_WORKAROUNDS_REMOVAL_GUIDE.md for removal instructions
# ============================================================================
# Set default for Qt NFC workarounds based on Android NFC backend usage
if(USE_ANDROID_NFC_BACKEND)
    option(ENABLE_QT_NFC_ANDROID_WORKAROUNDS
           "Enable workarounds for Qt 6.9.x NFC bugs on Android" OFF)
else()
    option(ENABLE_QT_NFC_ANDROID_WORKAROUNDS
           "Enable workarounds for Qt 6.9.x NFC bugs on Android" ON)
endif()

# Add Android-specific sources
if(ANDROID)
    # Always include android_jni_register.cpp for Android NFC handling
    # Either for Qt NFC workarounds or Android NFC backend
    if(ENABLE_QT_NFC_ANDROID_WORKAROUNDS OR USE_ANDROID_NFC_BACKEND)
        if(ENABLE_QT_NFC_ANDROID_WORKAROUNDS AND USE_ANDROID_NFC_BACKEND)
            message(STATUS "Android NFC: Both Qt NFC workarounds and Android NFC backend enabled")
        elseif(ENABLE_QT_NFC_ANDROID_WORKAROUNDS)
            message(STATUS "Qt NFC Android workarounds: ENABLED (for Qt 6.9.x bugs)")
            message(STATUS "  - QtNative.onNewIntent() JNI registration")
            message(STATUS "  - Manual QtNfc.startDiscovery() call")
            message(STATUS "  - Direct tag processing and signal emission")
        elseif(USE_ANDROID_NFC_BACKEND)
            message(STATUS "Android NFC backend: ENABLED (direct JNI implementation)")
            message(STATUS "  - NFC intent processing for direct Android NFC")
        endif()

        list(APPEND KEYCARD_QT_SOURCES
            src/channel/android_jni_register.cpp
        )
        list(APPEND KEYCARD_QT_HEADERS
            include/keycard-qt/qt_nfc_android_workarounds.h
        )
    else()
        message(STATUS "Android NFC: No special handling enabled")
        message(STATUS "  Re-enable Qt workarounds with: -DENABLE_QT_NFC_ANDROID_WORKAROUNDS=ON")
        message(STATUS "  Or use Android NFC backend with: -DUSE_ANDROID_NFC_BACKEND=ON")
    endif()
endif()

# Public headers
set(KEYCARD_QT_HEADERS
    include/keycard-qt/channel_interface.h
    include/keycard-qt/keycard_channel.h
    include/keycard-qt/command_set.h
    include/keycard-qt/secure_channel.h
    include/keycard-qt/types.h
    include/keycard-qt/apdu/command.h
    include/keycard-qt/apdu/response.h
    include/keycard-qt/apdu/utils.h
    # Backend interface
    include/keycard-qt/backends/keycard_channel_backend.h
)

# Add platform-specific backend headers
if(IOS)
    list(APPEND KEYCARD_QT_HEADERS
        include/keycard-qt/backends/keycard_channel_qt_nfc.h
    )
elseif(ANDROID)
    if(USE_ANDROID_NFC_BACKEND)
        list(APPEND KEYCARD_QT_HEADERS
            include/keycard-qt/backends/keycard_channel_android_nfc.h
        )
    else()
        list(APPEND KEYCARD_QT_HEADERS
            include/keycard-qt/backends/keycard_channel_qt_nfc.h
        )
    endif()
else()
    list(APPEND KEYCARD_QT_HEADERS
        include/keycard-qt/backends/keycard_channel_pcsc.h
    )
endif()

# Create library (shared or static based on BUILD_SHARED_LIBS)
add_library(keycard-qt
    ${KEYCARD_QT_SOURCES}
    ${KEYCARD_QT_HEADERS}
)

# Suppress OpenSSL 3.0 deprecation warnings (we use legacy APIs intentionally)
if(OpenSSL_FOUND)
    target_compile_definitions(keycard-qt PRIVATE OPENSSL_SUPPRESS_DEPRECATED)
endif()

# Include directories
target_include_directories(keycard-qt
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(keycard-qt
    PUBLIC
        Qt6::Core
)

# Qt NFC for iOS and Android
if(IOS OR ANDROID)
    target_link_libraries(keycard-qt PUBLIC Qt6::Nfc)
    if(IOS)
        message(STATUS "Linking Qt NFC for iOS")
    else()
        message(STATUS "Linking Qt NFC for Android")
    endif()
endif()

# Android JNI support
if(ANDROID)
    find_package(Qt6 COMPONENTS CorePrivate)
    if(Qt6CorePrivate_FOUND)
        target_link_libraries(keycard-qt PRIVATE Qt6::CorePrivate)
    endif()
    message(STATUS "Android build: JNI support enabled")
endif()

# Link OpenSSL (if found)
if(OpenSSL_FOUND)
    # For Android with manually provided paths, link directly to the library file
    if(ANDROID AND OPENSSL_CRYPTO_LIBRARY)
        message(STATUS "Linking against OpenSSL crypto library: ${OPENSSL_CRYPTO_LIBRARY}")
        target_link_libraries(keycard-qt PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
        
        # Add include directories
        # For OpenSSL 3.x on Android, we can use source headers directly
        # The generated build headers are not strictly necessary if using modern OpenSSL
        set(OPENSSL_INCLUDES_AVAILABLE FALSE)
        
        # Check if source headers exist (these are the main API headers)
        if(EXISTS "${OPENSSL_SOURCE_INCLUDE_DIR}")
            message(STATUS "Adding OpenSSL source include dir: ${OPENSSL_SOURCE_INCLUDE_DIR}")
            target_include_directories(keycard-qt PRIVATE ${OPENSSL_SOURCE_INCLUDE_DIR})
            set(OPENSSL_INCLUDES_AVAILABLE TRUE)
            
            # Also add build dir if it exists (has generated configuration.h)
            if(EXISTS "${OPENSSL_BUILD_INCLUDE_DIR}")
                message(STATUS "Adding OpenSSL build include dir: ${OPENSSL_BUILD_INCLUDE_DIR}")
                target_include_directories(keycard-qt PRIVATE ${OPENSSL_BUILD_INCLUDE_DIR})
            else()
                message(STATUS "OpenSSL build includes not found (using source headers only)")
            endif()
        else()
            message(WARNING "OpenSSL source include dir not found: ${OPENSSL_SOURCE_INCLUDE_DIR}")
            message(WARNING "OpenSSL headers not available. Building WITHOUT OpenSSL support.")
            message(WARNING "Crypto features will use fallback implementations.")
        endif()
        
        # Define KEYCARD_QT_HAS_OPENSSL if we have headers AND library
        if(OPENSSL_INCLUDES_AVAILABLE AND EXISTS "${OPENSSL_CRYPTO_LIBRARY}")
            target_compile_definitions(keycard-qt PRIVATE KEYCARD_QT_HAS_OPENSSL)
            message(STATUS "OpenSSL support: ENABLED (library + headers available)")
        else()
            message(STATUS "OpenSSL support: DISABLED (missing library or headers)")
        endif()
    else()
        # For other platforms, use the imported target
        target_link_libraries(keycard-qt PRIVATE OpenSSL::Crypto)
        target_compile_definitions(keycard-qt PRIVATE KEYCARD_QT_HAS_OPENSSL)
    endif()
endif()

# Link PC/SC framework on Apple platforms (desktop only, not iOS)
if(APPLE AND NOT IOS)
    find_library(PCSC_LIBRARY PCSC)
    if(PCSC_LIBRARY)
        target_link_libraries(keycard-qt PRIVATE ${PCSC_LIBRARY})
        message(STATUS "Found PCSC framework: ${PCSC_LIBRARY}")
    else()
        message(WARNING "PCSC framework not found")
    endif()
endif()

# Link CoreNFC framework on iOS (required for NFC support)
if(IOS)
    find_library(CORENFC_LIBRARY CoreNFC)
    if(CORENFC_LIBRARY)
        target_link_libraries(keycard-qt PRIVATE ${CORENFC_LIBRARY})
        message(STATUS "Found CoreNFC framework: ${CORENFC_LIBRARY}")
    else()
        message(WARNING "CoreNFC framework not found - NFC will not work on iOS")
    endif()
endif()

# Link PC/SC library on Linux (not Android)
if(UNIX AND NOT APPLE AND NOT ANDROID)
    # Use pkg-config to get PC/SC include paths and libraries
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PCSC libpcsclite)
        if(PCSC_FOUND)
            target_include_directories(keycard-qt PRIVATE ${PCSC_INCLUDE_DIRS})
            target_link_libraries(keycard-qt PRIVATE ${PCSC_LIBRARIES})
            message(STATUS "Found PC/SC via pkg-config: ${PCSC_VERSION}")
        else()
            message(WARNING "PC/SC not found via pkg-config")
        endif()
    else()
        # Fallback to find_library if pkg-config not available
        find_library(PCSCLITE_LIBRARY pcsclite)
        if(PCSCLITE_LIBRARY)
            target_link_libraries(keycard-qt PRIVATE ${PCSCLITE_LIBRARY})
            message(STATUS "Found PC/SC library: ${PCSCLITE_LIBRARY}")
        else()
            message(WARNING "PC/SC library not found")
        endif()
    endif()
endif()

# Link winscard on Windows
if(WIN32)
    target_link_libraries(keycard-qt PRIVATE winscard)
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(keycard-qt PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
elseif(MSVC)
    target_compile_options(keycard-qt PRIVATE /W4)
endif()

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Properties
set_target_properties(keycard-qt PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    PUBLIC_HEADER "${KEYCARD_QT_HEADERS}"
)

# Installation
include(GNUInstallDirs)

install(TARGETS keycard-qt
    EXPORT keycard-qt-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/keycard-qt
)

# Install subdirectory headers
install(DIRECTORY include/keycard-qt/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/keycard-qt
    FILES_MATCHING PATTERN "*.h"
)

# Export targets
install(EXPORT keycard-qt-targets
    FILE keycard-qt-targets.cmake
    NAMESPACE keycard-qt::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/keycard-qt
)

# Package configuration
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/keycard-qt-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/keycard-qt-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/keycard-qt
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/keycard-qt-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/keycard-qt-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/keycard-qt-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/keycard-qt
)

# Testing
option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
option(BUILD_EXAMPLES "Build examples" OFF)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

