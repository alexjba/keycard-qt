# Status Desktop Integration with Gradle Dependency

## Overview

This guide shows how to integrate keycard-qt into status-desktop using Gradle dependency management instead of manually copying JAR files.

## Benefits Over Manual JAR Copy

| Manual JAR Copy | Gradle Dependency |
|----------------|------------------|
| ❌ Copy file manually | ✅ Automatic download |
| ❌ Track binary in git | ✅ Track version number only |
| ❌ Update = copy new file | ✅ Update = change version |
| ❌ Multiple copies per project | ✅ One dependency line |
| ❌ CI/CD needs JAR in repo | ✅ CI/CD downloads automatically |

## Step-by-Step Integration

### Step 1: Publish keycard-qt to Maven Local

This is a **one-time setup** (per machine) or part of your build script:

```bash
# In keycard-qt repository
cd android
./gradlew publishToMavenLocal
```

This publishes to `~/.m2/repository/im/status/keycard/keycard-qt-nfc/0.1.0/`

**Result:**
```bash
✅ Published 5 files:
   - keycard-qt-nfc-0.1.0.jar (4.7 KB)
   - keycard-qt-nfc-0.1.0.pom (Maven metadata)
   - keycard-qt-nfc-0.1.0-sources.jar (Source code)
   - keycard-qt-nfc-0.1.0-javadoc.jar (Documentation)
   - keycard-qt-nfc-0.1.0.module (Gradle metadata)
```

### Step 2: Create Gradle Build File in status-desktop

Create `status-desktop/android/fetch-dependencies.gradle`:

```gradle
// fetch-dependencies.gradle
// Fetches keycard-qt and other dependencies from Maven repositories

buildscript {
    repositories {
        mavenCentral()
    }
}

// This is a minimal Gradle script just for fetching dependencies
apply plugin: 'base'

repositories {
    mavenLocal()      // Check ~/.m2/repository first
    mavenCentral()    // Fallback to Maven Central
}

// Configuration for keycard dependencies
configurations {
    keycardNfc
}

dependencies {
    // Keycard Qt NFC support
    keycardNfc 'im.status.keycard:keycard-qt-nfc:0.1.0'
}

// Task to copy JAR to vendor directory
task fetchKeycardJar(type: Copy) {
    from configurations.keycardNfc
    into "${projectDir}/vendor/keycard-qt"
    
    doLast {
        println "✅ Fetched keycard-qt JAR to vendor/keycard-qt/"
        fileTree(dir: "${projectDir}/vendor/keycard-qt").each { file ->
            println "   - ${file.name}"
        }
    }
}

// Task to check if JAR exists and is up to date
task checkKeycardJar {
    doLast {
        def jarFile = file("${projectDir}/vendor/keycard-qt/keycard-qt-nfc-0.1.0.jar")
        if (jarFile.exists()) {
            println "✅ keycard-qt JAR exists: ${jarFile.name} (${jarFile.length()} bytes)"
        } else {
            println "❌ keycard-qt JAR missing! Run: make fetch-keycard-jar"
        }
    }
}

// Task to show dependency tree
task showKeycardDependencies {
    doLast {
        println "Keycard Qt NFC dependencies:"
        configurations.keycardNfc.resolvedConfiguration.resolvedArtifacts.each {
            println "  - ${it.moduleVersion.id}"
            println "    File: ${it.file.name}"
            println "    Size: ${it.file.length()} bytes"
        }
    }
}

// Default task
defaultTasks 'fetchKeycardJar'
```

### Step 3: Update status-desktop Makefile

Add targets to fetch dependencies:

```makefile
# status-desktop/Makefile

# ... existing variables ...

# Keycard Qt version
KEYCARD_QT_VERSION ?= 0.1.0

# Gradle wrapper for dependency management
GRADLE ?= gradle

# ... existing targets ...

# Fetch keycard-qt JAR from Maven repository
fetch-keycard-jar:
	@echo "Fetching keycard-qt JAR..."
	@mkdir -p vendor/keycard-qt
	@cd android && $(GRADLE) -b fetch-dependencies.gradle fetchKeycardJar

# Check if keycard JAR exists
check-keycard-jar:
	@cd android && $(GRADLE) -b fetch-dependencies.gradle checkKeycardJar

# Show keycard dependency info
show-keycard-deps:
	@cd android && $(GRADLE) -b fetch-dependencies.gradle showKeycardDependencies

# Update all dependencies (including keycard)
update-deps: fetch-keycard-jar
	@echo "✅ All dependencies updated"

# Mobile build with automatic dependency fetch
mobile-build: check-keycard-jar
	@if ! test -f vendor/keycard-qt/keycard-qt-nfc-$(KEYCARD_QT_VERSION).jar; then \
		echo "Keycard JAR missing, fetching..."; \
		$(MAKE) fetch-keycard-jar; \
	fi
	@# ... existing mobile build commands ...
	export QMAKE=$(QMAKE_ANDROID)
	# ... rest of build ...

.PHONY: fetch-keycard-jar check-keycard-jar show-keycard-deps update-deps
```

### Step 4: Update status-desktop's .pro or CMakeLists.txt

The JAR location stays the same, just fetched automatically:

**For .pro file:**
```qmake
android {
    QT_ANDROID_EXTRA_LIBS += \
        $$PWD/vendor/keycard-qt/keycard-qt-nfc-0.1.0.jar
}
```

**For CMakeLists.txt:**
```cmake
if(ANDROID)
    set(KEYCARD_QT_JAR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/keycard-qt/keycard-qt-nfc-0.1.0.jar")
    
    # Check if JAR exists
    if(NOT EXISTS "${KEYCARD_QT_JAR}")
        message(FATAL_ERROR 
            "Keycard Qt JAR not found!\n"
            "Run: make fetch-keycard-jar\n"
            "Or: cd android && gradle -b fetch-dependencies.gradle fetchKeycardJar"
        )
    endif()
    
    set_target_properties(StatusDesktop PROPERTIES
        QT_ANDROID_EXTRA_LIBS "${KEYCARD_QT_JAR}"
    )
endif()
```

### Step 5: Update .gitignore

Don't track the JAR in git:

```gitignore
# status-desktop/.gitignore

# Vendor dependencies (fetched via Gradle)
vendor/keycard-qt/*.jar
```

But DO track the version in your Gradle file or Makefile.

## Usage Examples

### Regular Development Workflow

```bash
# Initial setup (one time)
cd status-desktop
make fetch-keycard-jar

# Build
make mobile-build
# (JAR is automatically checked and fetched if missing)
```

### Updating keycard-qt Version

```bash
# Option 1: Edit android/fetch-dependencies.gradle
# Change: keycardNfc 'im.status.keycard:keycard-qt-nfc:0.2.0'
make fetch-keycard-jar

# Option 2: Use Makefile variable
make fetch-keycard-jar KEYCARD_QT_VERSION=0.2.0
```

### CI/CD Pipeline

```yaml
# .github/workflows/mobile-build.yml

name: Build Android APK

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - name: Checkout
        uses: actions/checkout@v3
      
      - name: Setup Java
        uses: actions/setup-java@v3
        with:
          java-version: '11'
      
      - name: Setup Android SDK
        uses: android-actions/setup-android@v2
      
      # This step publishes keycard-qt to mavenLocal
      # Only needed if building keycard-qt from source
      - name: Build and Publish keycard-qt
        run: |
          git clone https://github.com/status-im/keycard-qt.git
          cd keycard-qt/android
          ./gradlew publishToMavenLocal
          cd ../..
      
      # Fetch dependencies (includes keycard-qt from mavenLocal)
      - name: Fetch Dependencies
        run: make fetch-keycard-jar
      
      # Build status-desktop
      - name: Build APK
        run: make mobile-build
```

## Advanced: Direct Gradle Dependency (Alternative)

If status-desktop fully adopts Gradle for Android builds, you can use it directly:

**android/build.gradle:**
```gradle
android {
    // ... android config ...
}

repositories {
    mavenLocal()
    mavenCentral()
}

dependencies {
    // Direct dependency - Gradle handles everything
    implementation 'im.status.keycard:keycard-qt-nfc:0.1.0'
    
    // ... other dependencies ...
}
```

No need for `fetch-dependencies.gradle` - Gradle downloads and includes automatically!

## Comparison: Current vs Gradle Approach

### Current Approach (Manual JAR)

```bash
# 1. Build keycard-qt
cd keycard-qt
cmake ... && make

# 2. Manually copy JAR
cp android/build/libs/keycard-qt-nfc-0.1.0.jar \
   ../status-desktop/vendor/keycard-qt/

# 3. Commit JAR to git
cd ../status-desktop
git add vendor/keycard-qt/keycard-qt-nfc-0.1.0.jar
git commit -m "Update keycard-qt"

# 4. Build
make mobile-build
```

### Gradle Dependency Approach

```bash
# 1. Build and publish keycard-qt (one-time or in CI)
cd keycard-qt/android
./gradlew publishToMavenLocal

# 2. Fetch in status-desktop (automatic or on-demand)
cd status-desktop
make fetch-keycard-jar  # Or: automatically in mobile-build

# 3. Build (JAR fetched automatically if missing)
make mobile-build
```

## Publishing Options for Production

### Option 1: Maven Local (Development)

**Current setup - good for:**
- Local development
- Single machine
- Testing

**Pros:** Simple, no server needed
**Cons:** Each dev must publish locally

### Option 2: GitHub Packages (Team)

**Setup:**
1. In keycard-qt, uncomment GitHub Packages in `android/build.gradle`
2. Publish: `GITHUB_TOKEN=... ./gradlew publish`
3. In status-desktop, add GitHub Packages repo:
   ```gradle
   repositories {
       maven {
           url = uri('https://maven.pkg.github.com/status-im/keycard-qt')
           credentials {
               username = System.getenv('GITHUB_ACTOR')
               password = System.getenv('GITHUB_TOKEN')
           }
       }
   }
   ```

**Pros:** Shared across team, version controlled
**Cons:** Needs GitHub token, private to org

### Option 3: Maven Central (Public)

**Setup:**
1. Register at https://central.sonatype.org/
2. Verify domain ownership for `im.status`
3. Setup GPG signing
4. Publish: `./gradlew publish`

**Pros:** Public, discoverable, standard
**Cons:** More setup, public only

### Option 4: Custom Maven Server

**Setup:**
- Host Artifactory, Nexus, or simple HTTP server
- Configure URL in both projects

**Pros:** Full control, can be private
**Cons:** Maintenance overhead

## Recommendation for status-desktop

### Phase 1: Maven Local (Now)

Start with `mavenLocal()` for development:

```gradle
repositories {
    mavenLocal()  // ~/.m2/repository
}

dependencies {
    keycardNfc 'im.status.keycard:keycard-qt-nfc:0.1.0'
}
```

**Implementation:**
1. Add `android/fetch-dependencies.gradle` (provided above)
2. Update Makefile with fetch targets
3. Update .gitignore to exclude JAR
4. Update build config to use `vendor/keycard-qt/keycard-qt-nfc-0.1.0.jar`

**Time:** ~30 minutes

### Phase 2: GitHub Packages (Later)

Move to GitHub Packages for CI/CD:

```gradle
repositories {
    maven {
        url = uri('https://maven.pkg.github.com/status-im/keycard-qt')
        credentials {
            username = System.getenv('GITHUB_ACTOR')
            password = System.getenv('GITHUB_TOKEN')
        }
    }
}
```

**Benefits:**
- CI/CD downloads automatically
- Version controlled
- Shared across team

**Time:** ~1 hour (including CI/CD setup)

### Phase 3: Maven Central (Future)

For public release, publish to Maven Central.

**Benefits:**
- Anyone can use keycard-qt
- Standard discovery
- No auth needed

**Time:** ~4 hours (including OSSRH registration)

## Testing

### Test Dependency Resolution

```bash
cd status-desktop/android
gradle -b fetch-dependencies.gradle showKeycardDependencies

# Should output:
# Keycard Qt NFC dependencies:
#   - im.status.keycard:keycard-qt-nfc:0.1.0
#     File: keycard-qt-nfc-0.1.0.jar
#     Size: 4812 bytes
```

### Test Fetch

```bash
cd status-desktop
rm -rf vendor/keycard-qt/*.jar  # Remove existing
make fetch-keycard-jar

# Should output:
# ✅ Fetched keycard-qt JAR to vendor/keycard-qt/
#    - keycard-qt-nfc-0.1.0.jar
```

### Test Build

```bash
make mobile-build
# Should build successfully with fetched JAR
```

## Troubleshooting

### "Could not find im.status.keycard:keycard-qt-nfc:0.1.0"

**Solution:**
```bash
# Publish keycard-qt to Maven Local first
cd /path/to/keycard-qt/android
./gradlew publishToMavenLocal

# Verify it's there
ls ~/.m2/repository/im/status/keycard/keycard-qt-nfc/0.1.0/
```

### "JAR not included in APK"

**Solution:**
```bash
# Check JAR exists
ls vendor/keycard-qt/keycard-qt-nfc-0.1.0.jar

# Check build config references it
grep "keycard-qt-nfc" *.pro  # or CMakeLists.txt

# Verify path is correct (absolute vs relative)
```

### Gradle fails with "ANDROID_HOME not set"

**Solution:**
```bash
export ANDROID_HOME=/path/to/android-sdk
# or
export ANDROID_SDK_ROOT=/path/to/android-sdk

# Add to your shell profile (~/.bashrc, ~/.zshrc)
echo 'export ANDROID_HOME=/path/to/android-sdk' >> ~/.zshrc
```

## Migration Checklist

- [ ] Publish keycard-qt to Maven Local
  ```bash
  cd keycard-qt/android && ./gradlew publishToMavenLocal
  ```

- [ ] Create `fetch-dependencies.gradle` in status-desktop
  ```bash
  cp example-fetch-dependencies.gradle android/fetch-dependencies.gradle
  ```

- [ ] Update Makefile with fetch targets
  ```makefile
  fetch-keycard-jar: ...
  ```

- [ ] Update .gitignore
  ```gitignore
  vendor/keycard-qt/*.jar
  ```

- [ ] Remove old JAR from git (if tracked)
  ```bash
  git rm vendor/keycard-qt/keycard-qt-nfc-*.jar
  ```

- [ ] Test fetch
  ```bash
  make fetch-keycard-jar
  ```

- [ ] Test build
  ```bash
  make mobile-build
  ```

- [ ] Update documentation
  - Add to README: "Run `make fetch-keycard-jar` to fetch dependencies"
  - Document version update process

- [ ] Update CI/CD
  - Add fetch step before build
  - Ensure ANDROID_HOME is set

## Summary

With Gradle dependency management:

✅ **Automatic** - No manual JAR copying
✅ **Versioned** - Clear version management
✅ **Standard** - How Android developers expect it
✅ **CI/CD Ready** - Works in automated builds
✅ **Maintainable** - Easy to update and rollback
✅ **Clean Git** - No binaries in repository

**Recommended setup:** Start with `mavenLocal()` now, move to GitHub Packages for team collaboration.

This makes status-desktop's dependency on keycard-qt much cleaner! 🎉

