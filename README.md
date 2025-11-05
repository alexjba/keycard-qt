# keycard-qt

[![Tests](https://github.com/status-im/keycard-qt/actions/workflows/test.yml/badge.svg)](https://github.com/status-im/keycard-qt/actions/workflows/test.yml)

Cross-platform C++/Qt library for Keycard APDU API - a 1:1 replacement for [keycard-go](https://github.com/status-im/keycard-go).

## Features

- **Cross-platform**: Linux, macOS, Windows (PC/SC), Android, iOS (NFC)
- **Unified API**: Single codebase using Qt NFC for all platforms
- **Complete APDU support**: All Keycard commands implemented
- **Secure channel**: ECDH key exchange + AES-CBC encryption
- **Thread-safe**: Safe for concurrent access
- **Well-tested**: >50% code coverage
- **Self-contained**: Android support includes automatic JAR build (no manual file copying)

## Supported Platforms

| Platform | Communication | Status |
|----------|---------------|--------|
| Linux    | PC/SC | 🚧 In Development |
| macOS    | PC/SC | 🚧 In Development |
| Windows  | PC/SC | 🚧 In Development |
| Android  | NFC (IsoDep via JNI) | 🚧 In Development |
| iOS      | NFC (via Qt NFC + CoreNFC) | ✅ Configured (Needs Testing) |

## Requirements

### Build Dependencies

- CMake 3.16+
- Qt 6.9.2 or later
  - QtCore
  - QtNfc
- C++17 compatible compiler
- OpenSSL

### Runtime Dependencies

- Qt 6.9.2 runtime
- PC/SC daemon (desktop platforms)
  - Linux: pcscd
  - macOS: built-in
  - Windows: built-in
- NFC hardware (mobile platforms)

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
cmake --install . --prefix /usr/local
```

### Build Options

- `BUILD_TESTING=ON|OFF` - Build unit tests (default: ON)
- `BUILD_EXAMPLES=ON|OFF` - Build example applications (default: OFF)
- `BUILD_ANDROID_NFC_JAR=ON|OFF` - Build Android NFC helper JAR (default: ON for Android)
- `PUBLISH_ANDROID_NFC_JAR=ON|OFF` - Auto-publish JAR to Maven Local (default: ON for Android)
- `USE_ANDROID_NFC_BACKEND=ON|OFF` - Use direct Android NFC backend (default: ON)

### Android Build

For Android builds using the Android NFC backend (default), the library **automatically builds and publishes** a self-contained JAR to Maven Local:

```bash
mkdir build && cd build
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-21
make -j10
```

**What happens automatically (when using Android NFC backend):**
- Builds native library: `libkeycard-qt.so`
- Builds Android NFC JAR: `android/build/libs/keycard-qt-nfc-0.1.0.jar`
- **Publishes to Maven Local:** `~/.m2/repository/im/status/keycard/keycard-qt-nfc/0.1.0/`

**Note:** JAR is only built when `USE_ANDROID_NFC_BACKEND=ON` (default), as it contains Java helper classes needed for the Android NFC backend. If using Qt NFC backend (`-DUSE_ANDROID_NFC_BACKEND=OFF`), the JAR isn't needed and won't be built.

**Then in your Android app:**
```gradle
repositories {
    mavenLocal()
}

dependencies {
    implementation 'im.status.keycard:keycard-qt-nfc:0.1.0'
}
```

That's it! No manual JAR copying needed. See [Auto Maven Publish Guide](AUTO_MAVEN_PUBLISH_GUIDE.md) for details.

## Quick Start

```cpp
#include <keycard-qt/keycard_channel.h>
#include <keycard-qt/command_set.h>

// Create channel (works on all platforms!)
auto channel = new KeycardChannel();

// Start detection
channel->startDetection();

// Connect signals
connect(channel, &KeycardChannel::targetDetected, [](const QString& uid) {
    qDebug() << "Keycard detected:" << uid;
});

// Use command set
auto cmdSet = new CommandSet(channel);

// Select keycard applet
auto appInfo = cmdSet->select();
qDebug() << "Instance UID:" << appInfo.instanceUID;

// Pair with keycard
auto pairingInfo = cmdSet->pair("KeycardDefaultPairing");

// Open secure channel
cmdSet->openSecureChannel(pairingInfo.index, pairingInfo.key);

// Verify PIN
cmdSet->verifyPIN("123456");

// Export keys
auto keys = cmdSet->exportKeyExtended(true, false, 
                                      P2ExportKeyPrivateAndPublic, 
                                      "m/44'/60'/0'/0/0");
```

## Architecture

```
┌─────────────────────────────────────┐
│ CommandSet                          │
│ • High-level keycard operations     │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│ SecureChannel                       │
│ • ECDH + AES-CBC encryption         │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│ KeycardChannel (Qt NFC)             │
│ • Unified PC/SC + NFC support       │
│ • Works on all 5 platforms!         │
└─────────────────────────────────────┘
```

## Documentation

- [API Reference](docs/API.md)
- [**iOS NFC Quick Start**](docs/IOS_QUICK_START.md) - ⭐ **iOS NFC setup in 5 minutes!**
- [iOS NFC Setup Guide](docs/IOS_NFC_SETUP.md) - Complete iOS NFC documentation
- [**Auto Maven Publish Guide**](docs/AUTO_MAVEN_PUBLISH_GUIDE.md) - Android JAR auto-publishing
- [Gradle Dependency Usage](docs/GRADLE_DEPENDENCY_USAGE.md) - Complete Gradle guide
- [Gradle Quick Start](docs/GRADLE_DEPENDENCY_QUICKSTART.md) - TL;DR version
- [Status Desktop Gradle Integration](docs/STATUS_DESKTOP_GRADLE_INTEGRATION.md) - Integration guide
- [Android JAR Usage](docs/ANDROID_JAR_USAGE.md) - Manual JAR integration (legacy)
- [Status Desktop Integration](docs/STATUS_DESKTOP_INTEGRATION.md) - Manual approach (legacy)
- [Porting Guide](docs/PORTING_GUIDE.md)
- [Qt NFC Integration](https://doc.qt.io/qt-6/qtnfc-pcsc.html)

## Testing

The project includes comprehensive unit tests using Qt Test framework:

```bash
cd build
ctest --output-on-failure
```


## Credits

- Based on [keycard-go](https://github.com/status-im/keycard-go)
- Part of the [Status](https://status.im) ecosystem
- Uses [Qt](https://www.qt.io/) framework
- Cryptography via [OpenSSL](https://github.com/openssl/openssl)

## Contributing

Contributions welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) first.

## Related Projects

- [status-keycard-qt](../status-keycard-qt/) - High-level Session API
- [keycard-go](https://github.com/status-im/keycard-go) - Original Go implementation
- [Keycard](https://keycard.tech/) - Hardware keycard specification

