# Gradle Dependency Quick Start

## For keycard-qt Maintainers

### Publish to Maven Local (Development)

```bash
cd keycard-qt/android
./gradlew publishToMavenLocal
```

**Result:** Published to `~/.m2/repository/im/status/keycard/keycard-qt-nfc/0.1.0/`

### Publish to GitHub Packages (Team Collaboration)

```bash
export GITHUB_TOKEN=your_token
cd keycard-qt/android
# Uncomment GitHub Packages section in build.gradle first
./gradlew publish
```

### Update Version

```gradle
// android/build.gradle
version = '0.2.0'  // Change this
```

Then publish again.

---

## For status-desktop / Consumers

### Method 1: Simple Gradle Fetch (Recommended)

**Step 1:** Create `android/fetch-dependencies.gradle`:

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
    into "${projectDir}/vendor/keycard-qt"
}

defaultTasks 'fetchKeycardJar'
```

**Step 2:** Add to Makefile:

```makefile
fetch-keycard-jar:
	cd android && gradle -b fetch-dependencies.gradle
```

**Step 3:** Use it:

```bash
make fetch-keycard-jar
make mobile-build
```

### Method 2: Direct Gradle Dependency (If Using Gradle)

In your `build.gradle`:

```gradle
repositories {
    mavenLocal()
}

dependencies {
    implementation 'im.status.keycard:keycard-qt-nfc:0.1.0'
}
```

Gradle handles everything automatically!

---

## Quick Commands

### keycard-qt

```bash
# Build JAR
./gradlew build

# Publish locally
./gradlew publishToMavenLocal

# Check published files
ls ~/.m2/repository/im/status/keycard/keycard-qt-nfc/0.1.0/

# View POM
cat ~/.m2/repository/im/status/keycard/keycard-qt-nfc/0.1.0/keycard-qt-nfc-0.1.0.pom
```

### Consumer Project

```bash
# Fetch JAR
gradle -b android/fetch-dependencies.gradle fetchKeycardJar

# Check dependency resolution
gradle -b android/fetch-dependencies.gradle dependencies

# Show dependency info
gradle -b android/fetch-dependencies.gradle showKeycardDependencies
```

---

## Version Update

### Update keycard-qt

```bash
# In keycard-qt/android/build.gradle
version = '0.2.0'

# Publish
./gradlew publishToMavenLocal
```

### Update in Consumer

```gradle
// In fetch-dependencies.gradle
dependencies {
    keycardNfc 'im.status.keycard:keycard-qt-nfc:0.2.0'  // Update version
}
```

```bash
# Fetch new version
make fetch-keycard-jar
```

---

## Three Ways to Use

### Manual JAR Copy

```bash
cp keycard-qt/android/build/libs/keycard-qt-nfc-0.1.0.jar \
   status-desktop/vendor/
```

### Gradle Fetch (Recommended)

```bash
# One-time: publish to mavenLocal
cd keycard-qt/android && ./gradlew publishToMavenLocal

# In consumer: fetch
cd status-desktop
make fetch-keycard-jar  # Uses Gradle to download
```

### Direct Dependency

```gradle
dependencies {
    implementation 'im.status.keycard:keycard-qt-nfc:0.1.0'
}
```

---

## Publishing Options

| Repository | Audience | Auth | Best For |
|-----------|----------|------|----------|
| **Maven Local** | Single developer | None | Development |
| **GitHub Packages** | Team | GitHub token | Internal |
| **Maven Central** | Everyone | OSSRH account | Public |

---

## Troubleshooting

### "Could not find im.status.keycard:keycard-qt-nfc"

```bash
# Publish it first
cd keycard-qt/android
./gradlew publishToMavenLocal

# Check it's there
ls ~/.m2/repository/im/status/keycard/keycard-qt-nfc/0.1.0/
```

### "ANDROID_HOME not set"

```bash
export ANDROID_HOME=/path/to/android-sdk
```

### Gradle wrapper not found

```bash
cd keycard-qt/android
gradle wrapper  # Creates gradlew
```

---

## Integration Comparison

### Before (Manual)

```bash
# Build keycard-qt
cd keycard-qt && make

# Copy JAR manually
cp android/build/libs/keycard-qt-nfc-0.1.0.jar ../status-desktop/vendor/

# Git track binary
git add vendor/keycard-qt-nfc-0.1.0.jar
```

### After (Gradle)

```bash
# Publish keycard-qt (one-time per machine)
cd keycard-qt/android && ./gradlew publishToMavenLocal

# Use in status-desktop
cd status-desktop
make fetch-keycard-jar  # Automatic
```

---

## CI/CD Integration

```yaml
# .github/workflows/build.yml
- name: Publish keycard-qt
  run: |
    cd keycard-qt/android
    ./gradlew publishToMavenLocal

- name: Build status-desktop
  run: |
    cd status-desktop
    make fetch-keycard-jar
    make mobile-build
```

---

## Files Summary

**keycard-qt:**
- `android/build.gradle` - Builds and publishes JAR
- Publishing: `./gradlew publishToMavenLocal`

**status-desktop:**
- `android/fetch-dependencies.gradle` - Fetches JAR from Maven
- `Makefile` - Adds `fetch-keycard-jar` target
- `.gitignore` - Excludes `vendor/keycard-qt/*.jar`

---

## Next Steps

1. **Try it now:**
   ```bash
   cd keycard-qt/android
   ./gradlew publishToMavenLocal
   ls ~/.m2/repository/im/status/keycard/keycard-qt-nfc/0.1.0/
   ```

2. **Integrate into status-desktop:**
   - Create `fetch-dependencies.gradle`
   - Update Makefile
   - Test: `make fetch-keycard-jar`

3. **Choose publishing strategy:**
   - Development: Maven Local ✅
   - Team: GitHub Packages
   - Public: Maven Central

---

## Documentation

- **Detailed Guide:** `docs/GRADLE_DEPENDENCY_USAGE.md`
- **status-desktop Integration:** `docs/STATUS_DESKTOP_GRADLE_INTEGRATION.md`
- **Manual JAR Usage:** `docs/ANDROID_JAR_USAGE.md`

---

**Bottom Line:** `implementation 'im.status.keycard:keycard-qt-nfc:0.1.0'`

