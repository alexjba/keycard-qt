// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#pragma once

// ============================================================================
// QT NFC ANDROID WORKAROUNDS
// ============================================================================
//
// TEMPORARY WORKAROUNDS for Qt 6.9.x bugs on Android
// These fixes can be REMOVED when Qt fixes the following bugs:
//
// BUG #1: QtNative.onNewIntent() native method never registered in JNI
//   - Qt declares it in Java but never calls RegisterNatives()
//   - Impact: onNewIntent() never reaches C++ code
//   - Fix needed in Qt: Add JNI registration in JNI_OnLoad_qtNfc
//
// BUG #2: QNearFieldManager::startTargetDetection() doesn't enable foreground dispatch
//   - Qt never calls QtNfc.startDiscovery() which enables foreground dispatch
//   - Impact: NFC Intents never delivered to app
//   - Fix needed in Qt: Call startDiscovery() from startTargetDetection()
//
// BUG #3: targetDetected signal doesn't fire even with bugs #1 and #2 fixed
//   - Qt NFC's internal listener mechanism is broken
//   - Impact: Apps never notified of detected tags
//   - Fix needed in Qt: Fix QMainNfcNewIntentListener integration
//
// WHEN TO REMOVE THIS FILE:
//   1. Test with Qt 6.10+ or later versions
//   2. If bugs are fixed, set ENABLE_QT_NFC_ANDROID_WORKAROUNDS=0
//   3. Test NFC still works without workarounds
//   4. Delete this file and related code
//
// TESTED WITH:
//   - Qt 6.9.3 (bugs present, workarounds required)
//   - Qt 6.9.2 (bugs present, workarounds required)
//
// ============================================================================

// Control flag: Set to 0 when Qt fixes the bugs to disable workarounds
#ifndef ENABLE_QT_NFC_ANDROID_WORKAROUNDS
#define ENABLE_QT_NFC_ANDROID_WORKAROUNDS 1
#endif

#if defined(Q_OS_ANDROID) || defined(ANDROID)
#if ENABLE_QT_NFC_ANDROID_WORKAROUNDS

#include <jni.h>

namespace Keycard {
namespace QtNfcWorkarounds {

/**
 * @brief Initialize Qt NFC workarounds
 * 
 * Call this BEFORE creating QNearFieldManager.
 * 
 * What it does:
 *   - Fixes Qt Bug #1: Registers QtNative.onNewIntent() JNI method
 *   - Sets up callback mechanism for direct tag notifications
 * 
 * @return true if initialization succeeded, false otherwise
 */
bool initialize();

/**
 * @brief Enable Android NFC foreground dispatch
 * 
 * Call this AFTER QNearFieldManager::startTargetDetection().
 * 
 * What it does:
 *   - Fixes Qt Bug #2: Calls QtNfc.startDiscovery() to enable foreground dispatch
 * 
 * @return true if foreground dispatch enabled, false otherwise
 */
bool enableForegroundDispatch();

/**
 * @brief Register callback for direct NFC Intent notifications
 * 
 * What it does:
 *   - Fixes Qt Bug #3: Provides direct callback when NFC tag detected
 *   - Bypasses Qt NFC's broken signal mechanism
 * 
 * @param callback Function to call when NFC Intent arrives
 *                 Callback receives (JNIEnv*, jobject intent)
 */
void setIntentCallback(void (*callback)(JNIEnv* env, jobject intent));

/**
 * @brief Check if workarounds are active
 * 
 * @return true if workarounds are compiled in and active
 */
inline constexpr bool areWorkaroundsEnabled() {
    return ENABLE_QT_NFC_ANDROID_WORKAROUNDS;
}

/**
 * @brief Get Qt version these workarounds were tested with
 * 
 * @return Qt version string (e.g., "6.9.3")
 */
const char* getTestedQtVersion();

/**
 * @brief Get description of bugs being worked around
 * 
 * @return Multi-line string describing the Qt bugs
 */
const char* getBugDescription();

} // namespace QtNfcWorkarounds
} // namespace Keycard

#endif // ENABLE_QT_NFC_ANDROID_WORKAROUNDS
#endif // Q_OS_ANDROID || ANDROID

