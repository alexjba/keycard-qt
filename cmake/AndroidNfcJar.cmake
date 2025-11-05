# Android NFC JAR Build Configuration
# 
# This module handles building and publishing the Android NFC helper JAR
# when using the Android NFC backend (USE_ANDROID_NFC_BACKEND=ON)
#
# Provides:
#   - BUILD_ANDROID_NFC_JAR option
#   - PUBLISH_ANDROID_NFC_JAR option
#   - android-nfc-jar target
#   - Automatic dependency tracking for Java source files
#   - Maven Local publishing

# Only run this configuration when using Android NFC backend
if(NOT USE_ANDROID_NFC_BACKEND)
    return()
endif()

# Option to build the Android NFC JAR (default: ON when using Android NFC backend)
option(BUILD_ANDROID_NFC_JAR "Build Android NFC helper classes into a JAR" ON)

# Option to automatically publish to Maven Local after building
option(PUBLISH_ANDROID_NFC_JAR "Automatically publish Android NFC JAR to Maven Local" ON)

if(NOT BUILD_ANDROID_NFC_JAR)
    message(STATUS "Android NFC JAR build: DISABLED")
    message(STATUS "  Enable with: -DBUILD_ANDROID_NFC_JAR=ON")
    return()
endif()

# Find Gradle wrapper
set(GRADLE_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/android/gradlew")

if(NOT EXISTS "${GRADLE_WRAPPER}")
    message(WARNING "Gradle wrapper not found at ${GRADLE_WRAPPER}")
    message(WARNING "Android NFC JAR will not be built")
    message(WARNING "Run: cd android && gradle wrapper to generate it")
    return()
endif()

message(STATUS "Building Android NFC JAR with Gradle")

# Set Android SDK location for Gradle
if(DEFINED ENV{ANDROID_SDK_ROOT})
    set(ANDROID_HOME_FOR_GRADLE $ENV{ANDROID_SDK_ROOT})
elseif(DEFINED ENV{ANDROID_HOME})
    set(ANDROID_HOME_FOR_GRADLE $ENV{ANDROID_HOME})
else()
    message(WARNING "ANDROID_SDK_ROOT or ANDROID_HOME not set - JAR build may fail")
endif()

# JAR output location
set(ANDROID_NFC_JAR_NAME "keycard-qt-nfc-${PROJECT_VERSION}.jar")
set(ANDROID_NFC_JAR_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/android/build/libs/${ANDROID_NFC_JAR_NAME}")

# Determine Gradle tasks to run
if(PUBLISH_ANDROID_NFC_JAR)
    set(GRADLE_TASKS clean build publishToMavenLocal)
    set(BUILD_COMMENT "Building and publishing Android NFC JAR to Maven Local...")
else()
    set(GRADLE_TASKS clean build)
    set(BUILD_COMMENT "Building Android NFC JAR with Gradle...")
endif()

# Custom command to build (and optionally publish) the JAR
# Tracks Java source and Gradle config - rebuilds automatically on changes
add_custom_command(
    OUTPUT ${ANDROID_NFC_JAR_OUTPUT}
    DEPENDS 
        ${CMAKE_CURRENT_SOURCE_DIR}/android/src/main/java/im/status/keycard/android/KeycardNfcReader.java
        ${CMAKE_CURRENT_SOURCE_DIR}/android/build.gradle
        ${CMAKE_CURRENT_SOURCE_DIR}/android/settings.gradle
    COMMAND ${CMAKE_COMMAND} -E env 
        ANDROID_HOME=${ANDROID_HOME_FOR_GRADLE}
        ANDROID_SDK_ROOT=${ANDROID_HOME_FOR_GRADLE}
        ${GRADLE_WRAPPER} ${GRADLE_TASKS} -p ${CMAKE_CURRENT_SOURCE_DIR}/android
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/android
    COMMENT "${BUILD_COMMENT}"
    VERBATIM
)

# Create a target that depends on the JAR output
add_custom_target(android-nfc-jar ALL
    DEPENDS ${ANDROID_NFC_JAR_OUTPUT}
    COMMENT "Android NFC JAR is up to date"
)

# Install the JAR to share directory
install(FILES ${ANDROID_NFC_JAR_OUTPUT}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/keycard-qt
    COMPONENT android-nfc
)

message(STATUS "  JAR output: ${ANDROID_NFC_JAR_OUTPUT}")
message(STATUS "  Will be installed to: ${CMAKE_INSTALL_DATADIR}/keycard-qt/${ANDROID_NFC_JAR_NAME}")

if(PUBLISH_ANDROID_NFC_JAR)
    # Determine Maven Local path
    if(DEFINED ENV{HOME})
        set(MAVEN_LOCAL "$ENV{HOME}/.m2/repository")
    elseif(DEFINED ENV{USERPROFILE})
        set(MAVEN_LOCAL "$ENV{USERPROFILE}/.m2/repository")
    endif()
    
    message(STATUS "  Maven publishing: ENABLED")
    message(STATUS "  Maven coordinates: im.status.keycard:keycard-qt-nfc:${PROJECT_VERSION}")
    if(MAVEN_LOCAL)
        message(STATUS "  Maven Local: ${MAVEN_LOCAL}/im/status/keycard/keycard-qt-nfc/${PROJECT_VERSION}/")
    endif()
    message(STATUS "")
    message(STATUS "  After build, consumers can use:")
    message(STATUS "    repositories { mavenLocal() }")
    message(STATUS "    dependencies { implementation 'im.status.keycard:keycard-qt-nfc:${PROJECT_VERSION}' }")
else()
    message(STATUS "  Maven publishing: DISABLED")
    message(STATUS "  Consumers can include this JAR in their Android builds")
    message(STATUS "  Enable publishing with: -DPUBLISH_ANDROID_NFC_JAR=ON")
endif()

