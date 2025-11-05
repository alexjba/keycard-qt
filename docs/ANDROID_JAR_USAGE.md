# Using the Android NFC JAR

## Overview

The `keycard-qt` library includes Android NFC support through a Java helper class:
- `KeycardNfcReader.java` - Implements NFC card detection and communication via Android's IsoDep API

Previously, this Java file needed to be manually copied into the consumer project. Now it is built into a self-contained JAR file during the CMake build process.

## Building the JAR

### Automatic Build (Default)

When building `keycard-qt` for Android, the JAR is automatically built by default:

```bash
cd /path/to/keycard-qt
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
         -DANDROID_ABI=arm64-v8a \
         -DANDROID_PLATFORM=android-21
make
```

The JAR will be built at: `android/build/libs/keycard-qt-nfc-<version>.jar`

### Manual Build

You can also build the JAR manually:

```bash
cd /path/to/keycard-qt/android
./gradlew build
```

The JAR will be created in `build/libs/keycard-qt-nfc-<version>.jar`

### Disabling JAR Build

If you want to disable the automatic JAR build:

```bash
cmake .. -DBUILD_ANDROID_NFC_JAR=OFF ...
```

## Using the JAR in Your Android Project

### Method 1: Qt Android Project (Recommended)

If you're building with Qt's Android build system (like status-desktop), add the JAR to your project:

1. **Copy the JAR** to your project's Android dependencies:
   ```bash
   cp /path/to/keycard-qt/android/build/libs/keycard-qt-nfc-0.1.0.jar \
      /path/to/your-project/vendor/keycard-qt-nfc.jar
   ```

2. **Add to your Qt project file** (`.pro` or CMakeLists.txt):

   For `.pro` file:
   ```qmake
   android {
       QT_ANDROID_EXTRA_LIBS += \
           $$PWD/vendor/keycard-qt-nfc.jar
   }
   ```

   Or for CMakeLists.txt with Qt:
   ```cmake
   if(ANDROID)
       set_target_properties(YourApp PROPERTIES
           QT_ANDROID_EXTRA_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/keycard-qt-nfc.jar"
       )
   endif()
   ```

3. **The JAR will be automatically packaged** into your APK by Qt's build system.

### Method 2: Gradle Android Project

If you're using a standard Gradle-based Android project:

1. **Copy the JAR** to your `libs` directory:
   ```bash
   mkdir -p app/libs
   cp /path/to/keycard-qt/android/build/libs/keycard-qt-nfc-0.1.0.jar app/libs/
   ```

2. **Add to your build.gradle**:
   ```gradle
   dependencies {
       implementation files('libs/keycard-qt-nfc-0.1.0.jar')
   }
   ```

### Method 3: CMake FetchContent (Advanced)

For fully automated builds, you can use CMake's FetchContent:

```cmake
include(FetchContent)

FetchContent_Declare(
    keycard-qt
    GIT_REPOSITORY https://github.com/status-im/keycard-qt.git
    GIT_TAG main
)

FetchContent_MakeAvailable(keycard-qt)

if(ANDROID)
    # The JAR is built automatically as part of keycard-qt build
    set(KEYCARD_NFC_JAR "${keycard-qt_SOURCE_DIR}/android/build/libs/keycard-qt-nfc-0.1.0.jar")
    
    set_target_properties(YourApp PROPERTIES
        QT_ANDROID_EXTRA_LIBS "${KEYCARD_NFC_JAR}"
    )
endif()
```

## Installation

When you install `keycard-qt`, the JAR is installed to the data directory:

```bash
make install
# JAR installed to: <prefix>/share/keycard-qt/keycard-qt-nfc-<version>.jar
```

You can reference it from the installation directory:

```cmake
find_package(keycard-qt REQUIRED)

if(ANDROID)
    set(KEYCARD_NFC_JAR "${CMAKE_INSTALL_PREFIX}/share/keycard-qt/keycard-qt-nfc-0.1.0.jar")
    set_target_properties(YourApp PROPERTIES
        QT_ANDROID_EXTRA_LIBS "${KEYCARD_NFC_JAR}"
    )
endif()
```

## Verifying the JAR Contents

You can inspect what's inside the JAR:

```bash
jar tf android/build/libs/keycard-qt-nfc-0.1.0.jar
```

Expected output:
```
META-INF/
META-INF/MANIFEST.MF
im/
im/status/
im/status/keycard/
im/status/keycard/android/
im/status/keycard/android/KeycardNfcReader.class
```

## Package Name

The Java classes are in the package: `im.status.keycard.android`

This means your JNI code should reference it as:
```cpp
jclass readerClass = env->FindClass("im/status/keycard/android/KeycardNfcReader");
```

## Native Methods

The JAR contains these native methods that are implemented in the `keycard-qt` C++ code:

### KeycardNfcReader
- `private static native void onNativeTagConnected(long nativePtr, Object isoDep)`
- `private static native void onNativeTagDisconnected(long nativePtr)`

These are automatically registered and handled by the library, so you don't need to implement them yourself.

## AndroidManifest.xml Permissions

Your app still needs NFC permissions in `AndroidManifest.xml`:

```xml
<uses-permission android:name="android.permission.NFC" />
<uses-feature android:name="android.hardware.nfc" android:required="true" />
```

## Troubleshooting

### "Class not found" error

If you get a ClassNotFoundException at runtime:
1. Verify the JAR is included in your APK:
   ```bash
   unzip -l YourApp.apk | grep keycard-qt-nfc
   ```
2. Check that the JAR is listed in your build configuration
3. Ensure the package name matches: `im.status.keycard.android`

### Build fails with "ANDROID_HOME not set"

Set the environment variable before building:
```bash
export ANDROID_HOME=/path/to/android-sdk
# or
export ANDROID_SDK_ROOT=/path/to/android-sdk
```

### Gradle fails to download

If you're behind a corporate proxy or firewall:
```bash
# Set Gradle proxy in android/gradle.properties
systemProp.http.proxyHost=your.proxy.host
systemProp.http.proxyPort=8080
```

## Migration from Manual File Copying

If you were previously copying the Java files manually:

**Before:**
```bash
# Old approach - manual copy
cp /path/to/keycard-qt/android/src/main/java/im/status/keycard/android/*.java \
   /path/to/status-desktop/android/src/main/java/im/status/keycard/android/
```

**After:**
```bash
# New approach - just include the JAR
cp /path/to/keycard-qt/android/build/libs/keycard-qt-nfc-0.1.0.jar \
   /path/to/status-desktop/vendor/
```

Then update your Qt project file to include the JAR (see Method 1 above).

## Benefits

✅ **Self-contained**: No need to copy Java source files
✅ **Version controlled**: JAR version matches library version
✅ **Cleaner builds**: Source files stay in one place
✅ **Easier updates**: Just replace the JAR file
✅ **Standard practice**: Using JARs is the Android standard

## For Library Maintainers

### Updating the Java Code

1. Edit the Java files in `android/src/main/java/`
2. Rebuild: `cd android && ./gradlew build`
3. Test the new JAR in your consumer project
4. Commit the Java source files (not the JAR itself)
5. Users will rebuild the JAR when they build keycard-qt

### Versioning

The JAR version is set in `android/build.gradle`:
```gradle
version = '0.1.0'
```

This should match the CMakeLists.txt version for consistency.

### CI/CD Integration

For continuous integration, ensure:
1. ANDROID_HOME or ANDROID_SDK_ROOT is set
2. Gradle wrapper has execute permissions
3. The JAR build is included in your build/test pipeline

Example GitHub Actions:
```yaml
- name: Setup Android SDK
  uses: android-actions/setup-android@v2
  
- name: Build keycard-qt with Android support
  run: |
    mkdir build && cd build
    cmake .. -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake
    make
    
- name: Verify JAR was built
  run: test -f android/build/libs/keycard-qt-nfc-*.jar
```

