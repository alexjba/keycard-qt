# iOS NFC Setup Guide for Keycard

This guide explains how to enable NFC keycard functionality on iOS devices using Qt NFC and Apple's CoreNFC framework.

## Overview

The keycard-qt library supports iOS NFC through Qt's `QNearFieldTarget` API, which uses Apple's CoreNFC framework under the hood. Keycards use **ISO 7816-4 APDU** protocol (NfcTagType4), which is fully supported by Qt NFC on iOS.

## Prerequisites

- **iOS Device**: iPhone 7 or later with iOS 13.0+
- **Physical Keycard**: Status Keycard or compatible ISO 7816-4 smart card
- **Qt 6.9.2+**: With Qt NFC module
- **Xcode**: Latest version with iOS SDK
- **Development Certificate**: Apple Developer account for signing

## Architecture

```
┌─────────────────────────────────────────┐
│ Status Desktop App (QML/Nim)           │
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│ status-keycard-qt (Session Manager)     │
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│ keycard-qt (Qt NFC Backend)             │
│ - keycard_channel_qt_nfc.cpp            │
│ - Uses QNearFieldTarget::sendCommand()  │
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│ Qt NFC Module (Qt 6.9+)                 │
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│ CoreNFC Framework (iOS)                 │
│ - NFCTagReaderSession                   │
│ - NFCISO7816Tag                         │
└─────────────────────────────────────────┘
```

## Configuration Steps

### 1. CMake Configuration (Already Done ✅)

The `keycard-qt/CMakeLists.txt` already includes iOS support:

```cmake
# iOS automatically uses Qt NFC backend
if(IOS)
    message(STATUS "Backend: Qt NFC (iOS)")
    list(APPEND KEYCARD_QT_SOURCES
        src/channel/backends/keycard_channel_qt_nfc.cpp
    )
endif()

# Link CoreNFC framework (required for NFC)
if(IOS)
    find_library(CORENFC_LIBRARY CoreNFC)
    target_link_libraries(keycard-qt PRIVATE ${CORENFC_LIBRARY})
endif()
```

### 2. iOS Entitlements (Already Created ✅)

The `keycard-qt/ios/Entitlements.plist` file has been created with:

```xml
<key>com.apple.developer.nfc.readersession.formats</key>
<array>
    <string>TAG</string>
</array>
```

This enables **ISO 7816 tag reading** capability.

### 3. Info.plist Configuration (Already Done ✅)

The `status-desktop/mobile/ios/Info.plist` has been updated with:

```xml
<!-- NFC Usage Description -->
<key>NFCReaderUsageDescription</key>
<string>Status uses NFC to communicate with your Keycard hardware wallet...</string>

<!-- Keycard Applet AID -->
<key>com.apple.developer.nfc.readersession.iso7816.select-identifiers</key>
<array>
    <string>A0000008040001</string>
</array>
```

## Building for iOS

### From status-desktop Repository

```bash
cd /Users/alexjbanca/Repos/status-desktop

# Set the iOS Qt installation path
export QMAKE=/Users/alexjbanca/Qt/6.9.2/ios/bin/qmake

# Build the iOS app
make mobile-build V=3 -j10
```

This will:
1. Build OpenSSL for iOS
2. Build keycard-qt with Qt NFC backend
3. Build status-keycard-qt
4. Build status-go for iOS
5. Create the iOS app bundle

### Build Output

The iOS app will be located at:
```
status-desktop/mobile/bin/Status.app
```

## Xcode Project Setup

### 1. Add Entitlements to Xcode

In your Xcode project:

1. Select your app target
2. Go to "Signing & Capabilities"
3. Click "+ Capability"
4. Add **"Near Field Communication Tag Reading"**
5. In the entitlements file, ensure you have:
   ```xml
   <key>com.apple.developer.nfc.readersession.formats</key>
   <array>
       <string>TAG</string>
   </array>
   ```

### 2. Link CoreNFC Framework

The CMake build automatically links CoreNFC, but if building manually:

1. Select your app target
2. Go to "Build Phases"
3. Expand "Link Binary With Libraries"
4. Click "+" and add **CoreNFC.framework**

### 3. Verify Info.plist

Ensure your Info.plist contains:

```xml
<!-- Required: NFC Usage Description -->
<key>NFCReaderUsageDescription</key>
<string>Your usage description here</string>

<!-- Required: Keycard Applet AID -->
<key>com.apple.developer.nfc.readersession.iso7816.select-identifiers</key>
<array>
    <string>A0000008040001</string>
</array>
```

## How It Works

### 1. NFC Detection

```cpp
// In keycard_channel_qt_nfc.cpp
void KeycardChannelQtNfc::startDetection()
{
    // Start Qt NFC target detection
    m_manager->startTargetDetection(QNearFieldTarget::NdefAccess);
}
```

Qt NFC automatically:
- Creates NFCTagReaderSession
- Detects ISO 7816-4 tags (Keycards)
- Emits `targetDetected()` signal

### 2. APDU Communication

```cpp
QByteArray KeycardChannelQtNfc::transmit(const QByteArray& apdu)
{
    // Send APDU command to keycard
    QNearFieldTarget::RequestId requestId = m_target->sendCommand(apdu);
    
    // Wait for response (blocking with event loop)
    QEventLoop eventLoop;
    eventLoop.exec();
    
    return response;
}
```

Under the hood, Qt NFC:
- Uses `NFCISO7816Tag.sendCommand()` from CoreNFC
- Handles tag session lifecycle
- Manages NFC reader UI

### 3. Secure Channel

All keycard operations use secure channel:
1. **SELECT**: Select keycard applet (`A0000008040001`)
2. **PAIR**: Establish pairing (if not paired)
3. **OPEN_SECURE_CHANNEL**: Start encrypted session
4. **VERIFY_PIN**: Authenticate user
5. **EXPORT_KEY / SIGN**: Perform cryptographic operations

## Testing

### Prerequisites
- Physical iOS device (iPhone 7+ with NFC)
- Physical Keycard
- iOS 13.0 or later
- Developer provisioning profile

### Test Procedure

1. **Deploy to Device**:
   ```bash
   # Build and deploy
   make mobile-build
   # Install via Xcode or iOS App Signer
   ```

2. **Enable NFC Debugging**:
   - Open Xcode
   - Connect device
   - Window → Devices and Simulators
   - Enable "Connect via network" for wireless debugging

3. **Test NFC Detection**:
   - Open Status app
   - Navigate to Keycard settings
   - Tap "Scan Keycard"
   - Hold keycard near top of iPhone
   - Watch for:
     - `targetDetected()` signal
     - Card UID printed in logs
     - Connection established

4. **Check Logs**:
   ```bash
   # View iOS device logs
   xcrun simctl logverbose enable
   # Or use Console.app on macOS
   ```

   Look for:
   ```
   KeycardChannelQtNfc: Initialized (iOS/Android)
   KeycardChannelQtNfc: Starting target detection
   KeycardChannelQtNfc: NFC supported: true
   KeycardChannelQtNfc: Target detected - UID: <uid>
   KeycardChannelQtNfc: Transmitting APDU: <hex>
   KeycardChannelQtNfc: Received response: 9000
   ```

### Expected Behavior

✅ **Success Indicators**:
- NFC icon appears in iOS status bar
- System NFC reader UI shows up
- Keycard detected within 1-2 seconds
- APDU communication succeeds (SW=9000)
- Secure channel established
- User can perform keycard operations

❌ **Common Issues**:
- **No NFC detection**: Check entitlements and Info.plist
- **"NFC not supported"**: Verify CoreNFC framework linked
- **Permission denied**: Check NFCReaderUsageDescription
- **Tag lost quickly**: iOS has 60-second tag timeout
- **APDU timeout**: Increase timeout in transmit()

## Troubleshooting

### Issue: NFC Not Detected

**Symptoms**: `m_manager->isSupported()` returns false

**Solutions**:
1. Verify CoreNFC framework is linked
2. Check Info.plist has NFCReaderUsageDescription
3. Ensure device supports NFC (iPhone 7+, iOS 13+)
4. Rebuild project completely

### Issue: Permission Denied

**Symptoms**: App crashes when starting NFC

**Solutions**:
1. Add entitlements file to Xcode target
2. Enable "Near Field Communication Tag Reading" capability
3. Verify provisioning profile includes NFC entitlement
4. Check bundle identifier matches profile

### Issue: Keycard Not Detected

**Symptoms**: NFC works but keycard not found

**Solutions**:
1. Verify Keycard AID in Info.plist: `A0000008040001`
2. Hold keycard flat against iPhone (near top edge)
3. Keep keycard still for 2-3 seconds
4. Try different orientation (landscape vs portrait)

### Issue: APDU Timeout

**Symptoms**: transmit() times out after 5 seconds

**Solutions**:
1. Increase timeout in transmit():
   ```cpp
   timeout.setInterval(30000);  // 30 seconds
   ```
2. Keep keycard very close to phone
3. Don't move keycard during operation
4. Check iOS device isn't in Low Power Mode

## iOS-Specific Limitations

### What Works ✅
- **ISO 7816-4 APDU**: Full support (our use case)
- **Tag Detection**: Automatic with Qt NFC
- **sendCommand()**: Bidirectional APDU communication
- **Secure Channel**: ECDH + AES-256-CBC encryption
- **All Keycard Operations**: SELECT, PAIR, SIGN, etc.

### What Doesn't Work ❌
- **NDEF Messages**: iOS doesn't support NDEF via Qt (we don't need this)
- **Background Tag Reading**: iOS only supports foreground NFC
- **Auto-launch on Tag**: Must be in-app to detect tags

### iOS vs Android Differences

| Feature | iOS | Android |
|---------|-----|---------|
| NFC API | CoreNFC (via Qt) | IsoDep (direct JNI) |
| Tag Detection | Automatic (Qt) | Manual Intent handling |
| APDU Support | ✅ Full | ✅ Full |
| Background NFC | ❌ No | ✅ Yes (with Intent filters) |
| Tag Timeout | 60 seconds | Indefinite |
| Required Permissions | Entitlements | Manifest |

## Code Paths

### iOS-Specific Code
```cpp
// keycard_channel_qt_nfc.cpp lines 425-477
// Standard Qt NFC path (used for iOS)
if (!m_target) {
    throw std::runtime_error("Not connected to any target");
}

QNearFieldTarget::RequestId requestId = m_target->sendCommand(apdu);
// Wait for response...
```

### Android-Specific Code
```cpp
// keycard_channel_qt_nfc.cpp lines 320-422
#if defined(Q_OS_ANDROID)
#if ENABLE_QT_NFC_ANDROID_WORKAROUNDS
    // Direct IsoDep access (workaround for Qt bugs)
    QJniObject responseArray = g_activeIsoDep.callObjectMethod(
        "transceive", "([B)[B", apduArray
    );
#endif
#endif
```

## Performance Characteristics

| Operation | iOS | Notes |
|-----------|-----|-------|
| Tag Detection | ~1-2s | Via CoreNFC |
| APDU Round-trip | 50-200ms | Depends on command |
| SELECT Applet | ~100ms | First APDU |
| PAIR/Open Channel | ~300ms | ECDH + crypto |
| VERIFY_PIN | ~200ms | PIN verification |
| EXPORT_KEY | ~400ms | Key export |
| SIGN | ~400ms | ECDSA signing |

## References

- [Qt NFC Documentation](https://doc.qt.io/qt-6/qtnfc-index.html)
- [Qt NFC Features](https://doc.qt.io/qt-6/qtnfc-features.html)
- [Apple CoreNFC](https://developer.apple.com/documentation/corenfc)
- [ISO 7816-4 APDU](https://en.wikipedia.org/wiki/Smart_card_application_protocol_data_unit)
- [Keycard Protocol](https://keycard.tech/)

## Next Steps

1. **Build iOS app**: Follow build instructions above
2. **Deploy to device**: Use Xcode or command-line tools
3. **Test with physical keycard**: Verify NFC detection
4. **Verify operations**: Test all keycard functions
5. **Performance tuning**: Optimize timeout values
6. **User testing**: Get feedback from iOS users

## Support

For issues or questions:
- Check [keycard-qt README](../README.md)
- See [status-keycard-qt docs](../../status-keycard-qt/README.md)
- Review Qt NFC examples: [Qt NFC Examples](https://doc.qt.io/qt-6/qtnfc-examples.html)

