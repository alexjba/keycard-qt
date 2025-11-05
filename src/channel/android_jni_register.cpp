// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

// ============================================================================
// QT NFC ANDROID WORKAROUNDS - Tag Processing
// ============================================================================
//
// THIS FILE CONTAINS WORKAROUNDS FOR QT 6.9.x BUGS
//
// Purpose: Process NFC Intents and extract tags when Qt NFC's internal
//          mechanism fails to emit targetDetected signals
//
// Related Header: keycard-qt/qt_nfc_android_workarounds.h
//
// REMOVE THIS FILE when Qt fixes the bugs and set:
//   ENABLE_QT_NFC_ANDROID_WORKAROUNDS=0
//
// ============================================================================

#if defined(Q_OS_ANDROID) || defined(ANDROID)

#include "keycard-qt/qt_nfc_android_workarounds.h"

#if ENABLE_QT_NFC_ANDROID_WORKAROUNDS

#include <jni.h>
#include <QtCore/private/qjnihelpers_p.h>
#include <QJniObject>
#include <QJniEnvironment>
#include <QDebug>

// Forward declare Qt's handleNewIntent (exists in QtCore but JNI method not registered)
namespace QtAndroidPrivate {
    void handleNewIntent(JNIEnv *env, jobject intent);
}

// Global callback for notifying KeycardChannel directly when NFC Intent arrives
// This bypasses Qt NFC's broken signal mechanism (Qt Bug #3)
namespace Keycard {
    // Forward declaration for Android NFC backend
    void processAndroidNfcIntent(JNIEnv* env, jobject intent);

    // Callback variable handling:
    // - When workarounds are enabled: Defined in keycard_channel_qt_nfc.cpp
    // - When workarounds are disabled: Defined here
    #if ENABLE_QT_NFC_ANDROID_WORKAROUNDS
    extern void (*g_nfcIntentCallback)(JNIEnv* env, jobject intent);
    #else
    void (*g_nfcIntentCallback)(JNIEnv* env, jobject intent) = nullptr;
    #endif
}

// ============================================================================
// WORKAROUND FOR QT BUG #1: JNI Method Registration
// ============================================================================

// JNI implementation of QtNative.onNewIntent()
// Qt declares this in Java but never registers the native method!
// Per Qt docs: https://doc.qt.io/qt-6/qtnfc-overview.html
static void JNICALL Java_org_qtproject_qt_android_QtNative_onNewIntent(
    JNIEnv *env, jclass /*clazz*/, jobject intent)
{
    qDebug() << "KeycardQt: onNewIntent called";
    
    // Forward intent to Qt's handler
    QtAndroidPrivate::handleNewIntent(env, intent);
    
    // Notify KeycardChannel callback if Qt NFC workarounds are enabled
#if ENABLE_QT_NFC_ANDROID_WORKAROUNDS
    if (Keycard::g_nfcIntentCallback) {
        Keycard::g_nfcIntentCallback(env, intent);
    }
#endif // ENABLE_QT_NFC_ANDROID_WORKAROUNDS

    // Notify Android NFC backend if it's being used
#ifdef USE_ANDROID_NFC_BACKEND
    Keycard::processAndroidNfcIntent(env, intent);
#endif // USE_ANDROID_NFC_BACKEND
}

// Manual JNI registration function
// Called from KeycardChannel constructor to ensure it happens early
// NOTE: This is in the global namespace but will be called from within Keycard namespace
namespace Keycard {

bool registerQtNativeOnNewIntent()
{
    qWarning() << "════════════════════════════════════════";
    qWarning() << "🔧 KeycardQt: Manual JNI registration starting";
    qWarning() << "🔧 Registering QtNative.onNewIntent()";
    qWarning() << "════════════════════════════════════════";
    
    // Get JavaVM from Qt
    JavaVM* vm = QtAndroidPrivate::javaVM();
    if (!vm) {
        qCritical() << "❌ Failed to get JavaVM from Qt";
        return false;
    }
    qWarning() << "✅ Got JavaVM from Qt";
    
    JNIEnv *env = nullptr;
    jint getEnvResult = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    
    bool needsDetach = false;
    if (getEnvResult == JNI_EDETACHED) {
        // Current thread not attached to JavaVM - attach it
        qWarning() << "⚠️  Thread not attached to JavaVM - attaching...";
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            qCritical() << "❌ Failed to attach thread to JavaVM";
            return false;
        }
        needsDetach = true;
        qWarning() << "✅ Thread attached to JavaVM";
    } else if (getEnvResult != JNI_OK) {
        qCritical() << "❌ Failed to get JNI environment";
        return false;
    }
    qWarning() << "✅ JNI environment obtained";
    
    // Find QtNative class using QtAndroidPrivate::findClass (handles classloader correctly)
    jclass qtNativeClass = QtAndroidPrivate::findClass("org/qtproject/qt/android/QtNative", env);
    if (!qtNativeClass) {
        qCritical() << "❌ Failed to find QtNative class";
        if (env->ExceptionCheck()) {
            qCritical() << "❌ JNI Exception:";
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        if (needsDetach) vm->DetachCurrentThread();
        return false;
    }
    qWarning() << "✅ Found QtNative class";
    
    // Register the onNewIntent method
    JNINativeMethod method = {
        const_cast<char*>("onNewIntent"),
        const_cast<char*>("(Landroid/content/Intent;)V"),
        reinterpret_cast<void*>(Java_org_qtproject_qt_android_QtNative_onNewIntent)
    };
    
    qWarning() << "🔧 Registering method: onNewIntent(Landroid/content/Intent;)V";
    
    if (env->RegisterNatives(qtNativeClass, &method, 1) < 0) {
        qCritical() << "❌ Failed to register onNewIntent JNI method";
        if (env->ExceptionCheck()) {
            qCritical() << "❌ JNI Exception:";
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        // Don't delete - Qt manages the global reference
        if (needsDetach) {
            vm->DetachCurrentThread();
        }
        return false;
    }
    
    qWarning() << "✅✅✅ Successfully registered onNewIntent JNI method!";
    qWarning() << "✅ Qt NFC should now receive new intents";
    qWarning() << "════════════════════════════════════════";
    
    // Note: QtAndroidPrivate::findClass returns a global reference
    // Don't delete it - Qt manages it
    // env->DeleteLocalRef(qtNativeClass);  // <- WRONG: causes crash
    
    // Detach thread if we attached it
    if (needsDetach) {
        vm->DetachCurrentThread();
        qWarning() << "✅ Thread detached from JavaVM";
    }
    
    return true;
}

// ============================================================================
// MANUALLY START NFC FOREGROUND DISPATCH
// ============================================================================

bool manuallyStartNfcDiscovery()
{
    qWarning() << "════════════════════════════════════════";
    qWarning() << "🔧 manuallyStartNfcDiscovery() called";
    qWarning() << "🔧 Goal: Enable NFC foreground dispatch";
    qWarning() << "════════════════════════════════════════";
    
    JavaVM* vm = QtAndroidPrivate::javaVM();
    if (!vm) {
        qCritical() << "❌ Failed to get JavaVM from Qt";
        return false;
    }
    qWarning() << "✅ Got JavaVM from Qt";
    
    JNIEnv *env = nullptr;
    jint getEnvResult = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    
    bool needsDetach = false;
    if (getEnvResult == JNI_EDETACHED) {
        qWarning() << "⚠️  Thread not attached to JavaVM - attaching...";
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            qCritical() << "❌ Failed to attach thread to JavaVM";
            return false;
        }
        needsDetach = true;
        qWarning() << "✅ Thread attached to JavaVM";
    } else if (getEnvResult != JNI_OK) {
        qCritical() << "❌ Failed to get JNI environment";
        return false;
    }
    
    // Find QtNfc class
    jclass qtNfcClass = QtAndroidPrivate::findClass("org/qtproject/qt/android/nfc/QtNfc", env);
    if (!qtNfcClass) {
        qCritical() << "❌ Failed to find QtNfc class";
        if (env->ExceptionCheck()) {
            qCritical() << "❌ JNI Exception:";
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        if (needsDetach) vm->DetachCurrentThread();
        return false;
    }
    qWarning() << "✅ Found QtNfc class";
    
    // Find startDiscovery method (NO parameters!)
    jmethodID startDiscoveryMethod = env->GetStaticMethodID(qtNfcClass, "startDiscovery", "()Z");
    if (!startDiscoveryMethod) {
        qCritical() << "❌ Failed to find QtNfc.startDiscovery() method";
        if (env->ExceptionCheck()) {
            qCritical() << "❌ JNI Exception:";
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        if (needsDetach) vm->DetachCurrentThread();
        return false;
    }
    qWarning() << "✅ Found QtNfc.startDiscovery() method";
    
    // Call startDiscovery() with NO parameters
    qWarning() << "🔧 Calling QtNfc.startDiscovery()...";
    jboolean result = env->CallStaticBooleanMethod(qtNfcClass, startDiscoveryMethod);
    
    if (env->ExceptionCheck()) {
        qCritical() << "❌ Exception during QtNfc.startDiscovery() call:";
        env->ExceptionDescribe();
        env->ExceptionClear();
        if (needsDetach) vm->DetachCurrentThread();
        return false;
    }
    
    if (result) {
        qWarning() << "✅✅✅ QtNfc.startDiscovery() returned TRUE!";
        qWarning() << "✅ NFC foreground dispatch is NOW ENABLED!";
    } else {
        qWarning() << "⚠️  QtNfc.startDiscovery() returned FALSE";
    }
    
    qWarning() << "════════════════════════════════════════";
    
    if (needsDetach) {
        vm->DetachCurrentThread();
        qWarning() << "✅ Thread detached from JavaVM";
    }
    
    return result;
}

} // namespace Keycard

#endif // ENABLE_QT_NFC_ANDROID_WORKAROUNDS

// ============================================================================
// ANDROID NFC BACKEND INTEGRATION
// ============================================================================
// When using the Android NFC backend, we need to intercept NFC intents
// and forward them to the backend for processing

#ifdef USE_ANDROID_NFC_BACKEND
#include "keycard-qt/backends/keycard_channel_android_nfc.h"
#include <QDebug>
#endif // USE_ANDROID_NFC_BACKEND

// Callback function that gets called from Qt's onNewIntent override
// This allows the Android NFC backend to process NFC intents
namespace Keycard {
void processAndroidNfcIntent(JNIEnv* env, jobject intent) {
    Q_UNUSED(env)
    Q_UNUSED(intent)

#ifdef USE_ANDROID_NFC_BACKEND
    qDebug() << "Android NFC Backend: Processing NFC intent";

    // Convert JNI objects to QJniObjects
    QJniObject intentObj(intent);

    // Check if the Android NFC backend can handle this intent
    bool handled = KeycardChannelAndroidNfc::checkForNfcIntent(intentObj);

    if (handled) {
        qDebug() << "Android NFC Backend: Intent handled successfully";
    } else {
        qDebug() << "Android NFC Backend: Intent not handled (not an NFC intent)";
    }
#endif // USE_ANDROID_NFC_BACKEND
}
} // namespace Keycard

#endif // Q_OS_ANDROID || ANDROID

// ============================================================================
// END OF QT NFC ANDROID WORKAROUNDS
// ============================================================================
