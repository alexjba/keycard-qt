# Using keycard-qt as a Gradle Dependency

## Overview

Instead of manually copying the JAR file, consumers can add keycard-qt as a standard Gradle dependency. This is the most convenient approach for Android projects.

## Publishing Options

There are several ways to make the library available as a Gradle dependency:

### 1. Local Maven Repository (Simplest for Development)

Best for: Development, testing, or when you control both projects

#### Step 1: Build and Publish Locally

```bash
cd keycard-qt/android
./gradlew publishToMavenLocal
```

This publishes to your local Maven repository (`~/.m2/repository/`).

#### Step 2: Consumer Project Configuration

In your `build.gradle` (status-desktop or any Android app):

```gradle
repositories {
    mavenLocal()  // Add this to check local Maven first
    mavenCentral()
}

dependencies {
    implementation 'im.status.keycard:keycard-qt-nfc:0.1.0'
}
```

That's it! The JAR will be automatically included in your build.

### 2. GitHub Packages (Good for Internal Use)

Best for: Private/internal projects, GitHub-hosted repos

#### Step 1: Configure Publishing

Uncomment the GitHub Packages section in `android/build.gradle`:

```gradle
maven {
    name = 'GitHubPackages'
    url = uri('https://maven.pkg.github.com/status-im/keycard-qt')
    credentials {
        username = System.getenv('GITHUB_ACTOR')
        password = System.getenv('GITHUB_TOKEN')
    }
}
```

#### Step 2: Publish

```bash
export GITHUB_ACTOR=your-username
export GITHUB_TOKEN=your-personal-access-token
cd keycard-qt/android
./gradlew publish
```

#### Step 3: Consumer Configuration

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

dependencies {
    implementation 'im.status.keycard:keycard-qt-nfc:0.1.0'
}
```

### 3. Maven Central (Best for Public Libraries)

Best for: Public open-source projects

#### Requirements:
- Sonatype OSSRH account
- GPG signing key
- Domain ownership verification (for im.status group ID)

#### Step 1: Setup

1. Create account at https://central.sonatype.org/
2. Request access for `im.status` group ID
3. Setup GPG key for signing

#### Step 2: Configure Credentials

In `~/.gradle/gradle.properties`:
```properties
ossrhUsername=your-username
ossrhPassword=your-password
signing.keyId=your-key-id
signing.password=your-key-password
signing.secretKeyRingFile=/path/to/secring.gpg
```

#### Step 3: Update build.gradle

Uncomment Maven Central section and add signing:

```gradle
plugins {
    id 'signing'
}

signing {
    sign publishing.publications.maven
}
```

#### Step 4: Publish

```bash
cd keycard-qt/android
./gradlew publish
```

#### Step 5: Consumer Configuration

```gradle
repositories {
    mavenCentral()  // Available to everyone!
}

dependencies {
    implementation 'im.status.keycard:keycard-qt-nfc:0.1.0'
}
```

### 4. Custom Maven Repository (Alternative)

You can also host your own Maven repository:

- **JFrog Artifactory** (enterprise)
- **Nexus Repository** (self-hosted)
- **Simple HTTP server** (basic needs)

## Integration with CMake

To automate publishing during CMake builds, update `CMakeLists.txt`:

```cmake
if(BUILD_ANDROID_NFC_JAR)
    # Option to publish to Maven Local after building
    option(PUBLISH_ANDROID_NFC_JAR "Publish Android NFC JAR to Maven Local" OFF)
    
    if(PUBLISH_ANDROID_NFC_JAR)
        add_custom_command(
            TARGET android-nfc-jar POST_BUILD
            COMMAND ${GRADLE_WRAPPER} publishToMavenLocal 
                -p ${CMAKE_CURRENT_SOURCE_DIR}/android
            COMMENT "Publishing Android NFC JAR to Maven Local..."
            VERBATIM
        )
    endif()
endif()
```

Then build with:
```bash
cmake .. -DPUBLISH_ANDROID_NFC_JAR=ON
make
```

## Consumer Examples

### Example 1: Qt Android App with Gradle

If your Qt app uses Gradle for Android builds:

**build.gradle:**
```gradle
android {
    // ... android config ...
}

repositories {
    mavenLocal()  // or mavenCentral()
}

dependencies {
    implementation 'im.status.keycard:keycard-qt-nfc:0.1.0'
}
```

### Example 2: Qt App Without Gradle

If you don't use Gradle but want to use the dependency system:

**build.gradle (new file):**
```gradle
plugins {
    id 'base'
}

repositories {
    mavenLocal()
}

configurations {
    keycardJar
}

dependencies {
    keycardJar 'im.status.keycard:keycard-qt-nfc:0.1.0'
}

task copyJar(type: Copy) {
    from configurations.keycardJar
    into 'vendor'
}
```

Then:
```bash
gradle copyJar  # Downloads JAR to vendor/
```

### Example 3: Status Desktop Integration

For status-desktop, which uses Qt's build system:

**Option A: Use mavenLocal() for development:**

1. Publish keycard-qt to Maven Local:
   ```bash
   cd keycard-qt/android
   ./gradlew publishToMavenLocal
   ```

2. Create `android/build.gradle` in status-desktop:
   ```gradle
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
       into "${projectDir}/vendor/keycard-qt"
       rename { "keycard-qt-nfc-0.1.0.jar" }
   }
   ```

3. Update Makefile:
   ```makefile
   mobile-build: fetch-keycard-jar
       # ... existing build commands ...
   
   fetch-keycard-jar:
       cd android && gradle fetchKeycardJar
   ```

**Option B: Hybrid approach (current + Gradle):**

Keep current setup but add optional Gradle fetch:

```bash
# Manual (current way)
cp keycard-qt/android/build/libs/keycard-qt-nfc-0.1.0.jar vendor/

# OR automatic (new way)
make fetch-keycard-jar  # Uses Gradle to download from mavenLocal()
```

## Version Management

### For Library Maintainers

When releasing a new version:

1. Update version in `android/build.gradle`:
   ```gradle
   version = '0.2.0'
   ```

2. Build and publish:
   ```bash
   cd android
   ./gradlew clean build publishToMavenLocal
   # or: ./gradlew publish (for remote repos)
   ```

3. Tag the release:
   ```bash
   git tag -a v0.2.0 -m "Release 0.2.0"
   git push origin v0.2.0
   ```

### For Consumers

To update to a new version:

```gradle
dependencies {
    implementation 'im.status.keycard:keycard-qt-nfc:0.2.0'  // Just change version
}
```

Or use dynamic versions:

```gradle
dependencies {
    // Latest 0.x version
    implementation 'im.status.keycard:keycard-qt-nfc:0.+'
    
    // Latest version (not recommended for production)
    implementation 'im.status.keycard:keycard-qt-nfc:+'
}
```

## Verification

### Verify Local Maven Publication

```bash
# Check if published
ls ~/.m2/repository/im/status/keycard/keycard-qt-nfc/0.1.0/

# Should contain:
# keycard-qt-nfc-0.1.0.jar
# keycard-qt-nfc-0.1.0.pom
# keycard-qt-nfc-0.1.0-sources.jar
# keycard-qt-nfc-0.1.0-javadoc.jar
```

### Verify Dependency Resolution

In consumer project:

```bash
./gradlew dependencies --configuration implementation

# Should show:
# +--- im.status.keycard:keycard-qt-nfc:0.1.0
```

### Verify JAR in APK

After building your APK:

```bash
unzip -l app/build/outputs/apk/debug/app-debug.apk | grep keycard

# Should show:
# im/status/keycard/android/KeycardNfcReader.class
```

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Build and Publish

on:
  push:
    tags:
      - 'v*'

jobs:
  publish:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Set up JDK
        uses: actions/setup-java@v3
        with:
          java-version: '11'
          
      - name: Setup Android SDK
        uses: android-actions/setup-android@v2
      
      - name: Build and Publish
        run: |
          cd android
          ./gradlew clean build publish
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

## Troubleshooting

### "Could not find im.status.keycard:keycard-qt-nfc:0.1.0"

**Cause:** JAR not published or repository not configured

**Solution:**
```bash
# Publish to local Maven
cd keycard-qt/android
./gradlew publishToMavenLocal

# Verify it's there
ls ~/.m2/repository/im/status/keycard/keycard-qt-nfc/0.1.0/

# Check consumer's repositories block includes mavenLocal()
```

### "Failed to resolve: im.status.keycard:keycard-qt-nfc"

**Cause:** Repository not accessible or credentials missing

**Solution:**
- Check repository URL is correct
- Verify credentials (for GitHub Packages, Maven Central)
- Try `gradle --refresh-dependencies`

### "Duplicate class" errors

**Cause:** JAR included both as dependency and manually

**Solution:**
- Remove manual JAR copy from `QT_ANDROID_EXTRA_LIBS`
- Only use Gradle dependency OR manual JAR, not both

## Benefits of Gradle Dependency Approach

✅ **Standard Practice** - How Android developers expect it
✅ **Version Management** - Easy to update, rollback, or pin versions
✅ **Automatic Download** - No manual file copying
✅ **Dependency Resolution** - Gradle handles everything
✅ **CI/CD Friendly** - Works seamlessly in automated builds
✅ **Transitive Dependencies** - Future dependencies handled automatically
✅ **IDE Integration** - Better auto-completion and navigation

## Comparison: JAR Copy vs Gradle Dependency

| Aspect | Manual JAR Copy | Gradle Dependency |
|--------|----------------|-------------------|
| Initial Setup | Copy file | Add one line |
| Updates | Copy new file | Change version number |
| CI/CD | Must have JAR in repo | Downloads automatically |
| Version Control | Track JAR binary | Track version number |
| Multiple Projects | Copy to each | All use same repository |
| Size in Git | ~5KB per copy | 0 bytes |
| Android Standard | Unusual | Standard practice |

## Recommendation

**For Development:** Use `mavenLocal()` (simplest)
```bash
cd keycard-qt/android && ./gradlew publishToMavenLocal
```

**For Internal/Team Use:** Use GitHub Packages
```bash
export GITHUB_TOKEN=...
./gradlew publish
```

**For Public Release:** Use Maven Central (most discoverable)
```bash
# After OSSRH setup
./gradlew publish
```

**For status-desktop:**
Start with `mavenLocal()` for development, then consider GitHub Packages for your CI/CD builds.

## Next Steps

1. **Publish locally** to test:
   ```bash
   cd keycard-qt/android
   ./gradlew publishToMavenLocal
   ```

2. **Update status-desktop** to use dependency:
   ```gradle
   repositories { mavenLocal() }
   dependencies { implementation 'im.status.keycard:keycard-qt-nfc:0.1.0' }
   ```

3. **Test the build** - Should work exactly the same

4. **Choose permanent solution** - Maven Central, GitHub Packages, or keep local

5. **Document in status-desktop** - How to fetch/update the dependency

The Gradle dependency approach is more maintainable and follows Android best practices! 🎉

