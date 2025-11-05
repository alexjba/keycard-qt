# Automatic Maven Publishing Guide

## What's Different Now

**Before:** You had to manually run `./gradlew publishToMavenLocal` after building

**Now:** Maven publishing happens **automatically** during CMake build when using Android NFC backend! 🎉

**Note:** The JAR is only built and published when `USE_ANDROID_NFC_BACKEND=ON` (the default for Android). If you're using Qt NFC backend, the JAR isn't needed and won't be built.

## Usage

### For keycard-qt Developers

Just build normally - publishing happens automatically:

```bash
cd keycard-qt
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
         -DANDROID_ABI=arm64-v8a
make
```

**Result:** JAR is automatically published to Maven Local!

```
 Built: android/build/libs/keycard-qt-nfc-0.1.0.jar
 Published to: ~/.m2/repository/im/status/keycard/keycard-qt-nfc/0.1.0/
```

### For Consumers (like status-desktop)

Just add the Gradle dependency:

```gradle
repositories {
    mavenLocal()
}

dependencies {
    implementation 'im.status.keycard:keycard-qt-nfc:0.1.0'
}
```

**That's it!** No manual JAR copying, no separate publish step.

## When Does JAR Build Happen?

The JAR is **only built when using Android NFC backend** (`USE_ANDROID_NFC_BACKEND=ON`).

| Backend | JAR Built? | Reason |
|---------|-----------|--------|
| **Android NFC (JNI)** | ✅ Yes | Requires Java helper classes |
| Qt NFC (Android) | ❌ No | Uses Qt's NFC framework |
| Qt NFC (iOS) | ❌ No | Not applicable |
| PC/SC (Desktop) | ❌ No | Not applicable |

**Default for Android:** `USE_ANDROID_NFC_BACKEND=ON` → JAR is built and published automatically.

## CMake Options

### Option 1: Automatic Publishing (Default for Android NFC Backend)

```bash
cmake .. # Publishing enabled by default
make
```

**What happens:**
- Builds JAR
- Publishes to Maven Local (`~/.m2/repository`)
- Ready to use in Gradle immediately

### Option 2: Build Only (No Publishing)

```bash
cmake .. -DPUBLISH_ANDROID_NFC_JAR=OFF
make
```

**What happens:**
- ✅ Builds JAR
- ❌ Does NOT publish to Maven
- ℹ️ You'll need to manually copy the JAR

### Option 3: Disable JAR Build Completely

```bash
cmake .. -DBUILD_ANDROID_NFC_JAR=OFF
make
```

**What happens:**
- ❌ No JAR build
- ❌ No Maven publishing

## CMake Output

When publishing is enabled, you'll see:

```
-- Building Android NFC JAR with Gradle
--   JAR output: /path/to/keycard-qt/android/build/libs/keycard-qt-nfc-0.1.0.jar
--   Will be installed to: share/keycard-qt/keycard-qt-nfc-0.1.0.jar
--   Maven publishing: ENABLED
--   Maven coordinates: im.status.keycard:keycard-qt-nfc:0.1.0
--   Maven Local: /Users/you/.m2/repository/im/status/keycard/keycard-qt-nfc/0.1.0/
--
--   After build, consumers can use:
--     repositories { mavenLocal() }
--     dependencies { implementation 'im.status.keycard:keycard-qt-nfc:0.1.0' }
```

## Integration Examples

### Example 1: Direct Gradle Dependency (Simplest)

In your Android project's `build.gradle`:

```gradle
repositories {
    mavenLocal()  // Check local Maven first
    mavenCentral() // Fallback
}

dependencies {
    implementation 'im.status.keycard:keycard-qt-nfc:0.1.0'
}
```

Build your project:
```bash
./gradlew build
# Gradle automatically finds and includes the JAR!
```

### Example 2: Qt Android Project

In your `.pro` file, you still need to reference the JAR:

```qmake
android {
    # Fetch from Maven first (optional helper script)
    system(cd android && gradle fetchKeycardJar)
    
    # Include the JAR
    QT_ANDROID_EXTRA_LIBS += $$PWD/vendor/keycard-qt/keycard-qt-nfc-0.1.0.jar
}
```

Create `android/fetch-dependencies.gradle`:
```gradle
apply plugin: 'base'

repositories {
    mavenLocal()
}

configurations {
    keycardNfc
}

dependencies {
    keycardNfc 'im.status.keycard:keycard-qt-nfc:0.1.0'
}

task fetchKeycardJar(type: Copy) {
    from configurations.keycardNfc
    into "${projectDir}/../vendor/keycard-qt"
}
```

### Example 3: CMakeLists.txt Integration

In your `CMakeLists.txt`:

```cmake
if(ANDROID)
    # Use Gradle to fetch from Maven Local
    execute_process(
        COMMAND gradle -b android/fetch-dependencies.gradle fetchKeycardJar
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    set(KEYCARD_JAR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/keycard-qt/keycard-qt-nfc-0.1.0.jar")
    
    set_target_properties(YourApp PROPERTIES
        QT_ANDROID_EXTRA_LIBS "${KEYCARD_JAR}"
    )
endif()
```

## Workflow Comparison

### Old Workflow (Manual)

```bash
# Step 1: Build keycard-qt
cd keycard-qt && mkdir build && cd build
cmake .. && make

# Step 2: Manually publish
cd ../android
./gradlew publishToMavenLocal

# Step 3: Use in consumer
cd ../../status-desktop
# Add Gradle dependency
./gradlew build
```

### New Workflow (Automatic) ✅

```bash
# Step 1: Build keycard-qt (publishing happens automatically!)
cd keycard-qt && mkdir build && cd build
cmake .. && make
# ✅ JAR is automatically published to Maven Local

# Step 2: Use in consumer
cd ../../status-desktop
# Add Gradle dependency (one-time setup)
./gradlew build
# ✅ Gradle finds it in Maven Local automatically
```

## Benefits

 **One Command** - Just `cmake .. && make`
 **Always in Sync** - Publishing happens every build
 **No Manual Steps** - Completely automatic
 **Standard Practice** - Consumers use standard Gradle dependencies
 **Version Controlled** - Version in CMakeLists.txt matches Maven artifact
 **CI/CD Ready** - Works seamlessly in automated builds

## Verification

### Verify JAR is Published

```bash
# Check Maven Local
ls ~/.m2/repository/im/status/keycard/keycard-qt-nfc/0.1.0/

# Should show:
# keycard-qt-nfc-0.1.0.jar
# keycard-qt-nfc-0.1.0.pom
# keycard-qt-nfc-0.1.0-sources.jar
# keycard-qt-nfc-0.1.0-javadoc.jar
# keycard-qt-nfc-0.1.0.module
```

### Verify Gradle Can Find It

```bash
# In consumer project
gradle dependencies --configuration implementation

# Should show:
# +--- im.status.keycard:keycard-qt-nfc:0.1.0
```

## Troubleshooting

### JAR Not Published

**Check CMake output:**
```bash
cmake .. | grep "Maven publishing"
# Should show: "Maven publishing: ENABLED"
```

**If disabled:**
```bash
cmake .. -DPUBLISH_ANDROID_NFC_JAR=ON
make
```

### "Could not find im.status.keycard:keycard-qt-nfc"

**Solution:**
1. Verify JAR is published:
   ```bash
   ls ~/.m2/repository/im/status/keycard/keycard-qt-nfc/0.1.0/
   ```

2. Ensure `mavenLocal()` is in repositories:
   ```gradle
   repositories {
       mavenLocal()  // Must be present!
   }
   ```

3. Rebuild keycard-qt to re-publish:
   ```bash
   cd keycard-qt/build
   make clean
   make
   ```

### ANDROID_HOME Not Set

```bash
export ANDROID_HOME=/path/to/android-sdk
# or
export ANDROID_SDK_ROOT=/path/to/android-sdk
```

Add to your shell profile:
```bash
echo 'export ANDROID_HOME=/path/to/android-sdk' >> ~/.zshrc
```

## Complete Example: status-desktop Integration

### Step 1: Build keycard-qt (one-time or after updates)

```bash
cd keycard-qt
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
         -DANDROID_ABI=arm64-v8a
make
# ✅ JAR automatically published to Maven Local
```

### Step 2: Add Gradle dependency in status-desktop

Create `android/build.gradle` (if using Gradle) or `android/fetch-dependencies.gradle` (if using Qt build):

```gradle
// For Qt projects: fetch-dependencies.gradle
apply plugin: 'base'

repositories {
    mavenLocal()
}

configurations {
    keycardNfc
}

dependencies {
    keycardNfc 'im.status.keycard:keycard-qt-nfc:0.1.0'
}

task fetchKeycardJar(type: Copy) {
    from configurations.keycardNfc
    into "${projectDir}/../vendor/keycard-qt"
}
```

### Step 3: Update Makefile

```makefile
# status-desktop/Makefile

fetch-keycard-jar:
	@echo "Fetching keycard-qt from Maven Local..."
	cd android && gradle -b fetch-dependencies.gradle fetchKeycardJar

mobile-build: fetch-keycard-jar
	# ... existing build commands ...
```

### Step 4: Build

```bash
cd status-desktop
make mobile-build
# Fetches JAR from Maven Local
# Includes in APK
# Ready to use!
```

## Summary

**The complete workflow is now:**

1. **Build keycard-qt** → JAR automatically published to Maven Local
2. **Add Gradle dependency** → Consumers use standard Gradle dependency
3. **Build consumer** → Gradle fetches from Maven Local automatically

**No manual JAR copying, no separate publish step, completely automatic!**

---

**Maven Coordinates:** `im.status.keycard:keycard-qt-nfc:0.1.0`

**Repository:** `mavenLocal()` (automatic after CMake build)

**Status:** ✅ Complete and Automatic

