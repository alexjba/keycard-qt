# Integrating keycard-qt Android JAR into status-desktop

## Overview

This guide shows how to integrate the `keycard-qt` Android NFC JAR into status-desktop, replacing the previous manual file copying approach.

## Previous Approach (Manual File Copying)

Previously, you had to manually copy Java files:

```bash
# OLD METHOD - No longer needed!
cp keycard-qt/android/src/main/java/im/status/keycard/android/*.java \
   status-desktop/android/src/main/java/im/status/keycard/android/
```

This approach had several problems:
- ❌ Files could get out of sync
- ❌ Manual step in build process
- ❌ Version tracking was difficult
- ❌ Source duplication

## New Approach (Self-Contained JAR)

Now the Java classes are built into a JAR automatically:

```bash
# NEW METHOD - Automatic!
# The JAR is built when you build keycard-qt
# Just include it in your status-desktop build
```

Benefits:
- ✅ Automatic build process
- ✅ Version controlled (JAR version matches library version)
- ✅ No file duplication
- ✅ Standard Android practice

## Integration Steps for status-desktop

### Step 1: Build keycard-qt with Android Support

When you build keycard-qt for Android, the JAR is automatically created:

```bash
cd /Users/alexjbanca/Repos/keycard-qt
mkdir -p build/android/arm64
cd build/android/arm64

cmake ../../.. \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-21 \
    -DCMAKE_BUILD_TYPE=Release

make -j10
```

This creates:
- The native library: `build/android/arm64/libkeycard-qt.so`
- The Android JAR: `android/build/libs/keycard-qt-nfc-0.1.0.jar`

### Step 2: Copy JAR to status-desktop Vendor Directory

Create a vendor directory for keycard-qt if it doesn't exist:

```bash
cd /Users/alexjbanca/Repos/status-desktop
mkdir -p vendor/keycard-qt/android
```

Copy the JAR:

```bash
cp /Users/alexjbanca/Repos/keycard-qt/android/build/libs/keycard-qt-nfc-0.1.0.jar \
   /Users/alexjbanca/Repos/status-desktop/vendor/keycard-qt/android/
```

### Step 3: Update status-desktop Build Configuration

#### Option A: Qt Project File (.pro)

If status-desktop uses a `.pro` file, add:

```qmake
android {
    # Include keycard-qt Android NFC JAR
    QT_ANDROID_EXTRA_LIBS += \
        $$PWD/vendor/keycard-qt/android/keycard-qt-nfc-0.1.0.jar
}
```

#### Option B: CMakeLists.txt

If status-desktop uses CMake for Android builds, add:

```cmake
if(ANDROID)
    # Set the path to keycard-qt Android JAR
    set(KEYCARD_QT_NFC_JAR 
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/keycard-qt/android/keycard-qt-nfc-0.1.0.jar"
    )
    
    # Verify JAR exists
    if(NOT EXISTS "${KEYCARD_QT_NFC_JAR}")
        message(FATAL_ERROR 
            "keycard-qt NFC JAR not found at: ${KEYCARD_QT_NFC_JAR}\n"
            "Please build keycard-qt for Android first and copy the JAR to vendor/"
        )
    endif()
    
    # Add to Qt Android extra libs
    set_target_properties(StatusDesktop PROPERTIES
        QT_ANDROID_EXTRA_LIBS "${KEYCARD_QT_NFC_JAR}"
    )
endif()
```

### Step 4: Remove Old Java Source Files (If Present)

If you previously copied the Java source files to status-desktop, you can now remove them:

```bash
cd /Users/alexjbanca/Repos/status-desktop

# Remove old source files (if they exist)
rm -rf android/src/main/java/im/status/keycard/android/KeycardNfcReader.java

# The directory might now be empty
rmdir android/src/main/java/im/status/keycard/android/ 2>/dev/null || true
```

### Step 5: Verify Integration

Build status-desktop for Android:

```bash
cd /Users/alexjbanca/Repos/status-desktop
export QMAKE=/Users/alexjbanca/Qt/6.9.3/android_arm64_v8a/bin/qmake
make mobile-build V=3 -j10
```

Verify the JAR is included in the APK:

```bash
# Find the built APK
APK_PATH=$(find mobile/bin -name "*.apk" | head -1)

# List contents and look for keycard classes
unzip -l "$APK_PATH" | grep -i keycard

# Expected output should include:
# im/status/keycard/android/KeycardNfcReader.class
```

## Makefile Integration for status-desktop

To automate this process, you can add a target to status-desktop's Makefile:

```makefile
# Path to keycard-qt
KEYCARD_QT_DIR ?= ../keycard-qt

# Update keycard-qt JAR
update-keycard-qt-jar:
	@echo "Copying keycard-qt Android JAR..."
	@mkdir -p vendor/keycard-qt/android
	@cp $(KEYCARD_QT_DIR)/android/build/libs/keycard-qt-nfc-*.jar \
	    vendor/keycard-qt/android/
	@echo "✅ keycard-qt JAR updated"

# Add dependency to mobile build
mobile-build: update-keycard-qt-jar
	# ... existing mobile build commands ...
```

Then building is simple:

```bash
make mobile-build
```

## Version Management

### Tracking JAR Version

Add a note in status-desktop's README or build docs:

```markdown
## keycard-qt Dependency

Android builds require the keycard-qt Android NFC JAR:
- Version: 0.1.0
- Location: `vendor/keycard-qt/android/keycard-qt-nfc-0.1.0.jar`
- Source: https://github.com/status-im/keycard-qt

To update:
1. Build keycard-qt for Android
2. Copy the JAR from `keycard-qt/android/build/libs/` to `vendor/keycard-qt/android/`
```

### Git Tracking

You have two options for the JAR in status-desktop's git:

#### Option 1: Track the JAR (Recommended for stability)

```bash
cd /Users/alexjbanca/Repos/status-desktop
git add vendor/keycard-qt/android/keycard-qt-nfc-0.1.0.jar
git commit -m "Add keycard-qt Android NFC JAR v0.1.0"
```

**Pros:**
- Build works immediately after cloning
- No external dependencies during build
- Version locked to specific commit

**Cons:**
- Binary in git (but JARs are small, this one is ~5KB)

#### Option 2: Don't track JAR, build on demand

Add to `.gitignore`:
```
vendor/keycard-qt/
```

And document in build instructions:
```bash
# After cloning status-desktop
cd status-desktop
make update-keycard-qt-jar  # Copies JAR from keycard-qt
make mobile-build
```

**Pros:**
- No binaries in git
- Always uses latest version

**Cons:**
- Requires keycard-qt to be built first
- Extra build step

## Troubleshooting

### JAR not found in APK

If the JAR classes aren't in your APK:

1. **Verify JAR path in build config**:
   ```bash
   # Check that the path in your .pro or CMakeLists.txt is correct
   ls -l vendor/keycard-qt/android/keycard-qt-nfc-0.1.0.jar
   ```

2. **Check Qt's Android build output**:
   ```bash
   # Look for messages about extra libs
   make mobile-build V=3 2>&1 | grep -i "extra.*lib"
   ```

3. **Verify APK contents**:
   ```bash
   unzip -l mobile/bin/StatusDesktop.apk | grep KeycardNfc
   ```

### ClassNotFoundException at runtime

If you get `java.lang.ClassNotFoundException: im.status.keycard.android.KeycardNfcReader`:

1. **Check package name in C++ code**:
   ```cpp
   // Should be:
   env->FindClass("im/status/keycard/android/KeycardNfcReader")
   // NOT:
   env->FindClass("KeycardNfcReader")  // ❌ Wrong
   ```

2. **Verify JAR is in APK** (see above)

3. **Check AndroidManifest.xml has NFC permissions**:
   ```xml
   <uses-permission android:name="android.permission.NFC" />
   ```

### Gradle build fails

If you can't build the JAR:

1. **Verify ANDROID_HOME is set**:
   ```bash
   echo $ANDROID_HOME
   # Should print: /Users/alexjbanca/Library/Android/sdk
   ```

2. **Build JAR manually**:
   ```bash
   cd keycard-qt/android
   ./gradlew clean build --info
   ```

3. **Check for required Android platform**:
   ```bash
   ls $ANDROID_HOME/platforms/
   # Should include android-33
   ```

## Automated Build Script

Here's a complete script to automate the update:

```bash
#!/bin/bash
# update-keycard-qt.sh - Updates keycard-qt JAR in status-desktop

set -e

SCRIPT_DIR=$(dirname "$0")
STATUS_DESKTOP_DIR="$SCRIPT_DIR"
KEYCARD_QT_DIR="${KEYCARD_QT_DIR:-$STATUS_DESKTOP_DIR/../keycard-qt}"

echo "🔧 Building keycard-qt Android JAR..."
cd "$KEYCARD_QT_DIR/android"
./gradlew clean build

echo "📦 Copying JAR to status-desktop..."
mkdir -p "$STATUS_DESKTOP_DIR/vendor/keycard-qt/android"
cp build/libs/keycard-qt-nfc-*.jar "$STATUS_DESKTOP_DIR/vendor/keycard-qt/android/"

echo "✅ keycard-qt JAR updated successfully!"
ls -lh "$STATUS_DESKTOP_DIR/vendor/keycard-qt/android/"
```

Save this to `status-desktop/scripts/update-keycard-qt.sh` and make it executable:

```bash
chmod +x scripts/update-keycard-qt.sh
./scripts/update-keycard-qt.sh
```

## Summary

The new JAR-based approach provides:

1. **Self-contained library** - No manual file copying
2. **Version control** - JAR version matches library version  
3. **Standard Android practice** - Using JARs is the Android way
4. **Cleaner builds** - One source of truth for Java code
5. **Easier updates** - Just replace the JAR file

The integration requires:
- Building keycard-qt for Android (creates the JAR)
- Copying JAR to status-desktop's vendor directory
- Adding one line to your build configuration
- No more manual Java file copying!

