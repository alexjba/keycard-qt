// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#include "keycard-qt/backends/keycard_channel_qt_nfc.h"
#include <QDebug>
#include <QThread>
#include <QCoreApplication>
#include <stdexcept>

#if defined(Q_OS_IOS)
#include <execinfo.h>  // For backtrace() - callstack debugging
#endif

// iOS: Define static global card session (persists across object reconstruction)
// This is critical because status-desktop destroys/recreates SessionManager after login
#if defined(Q_OS_IOS)
Keycard::KeycardChannelQtNfc::CardSession Keycard::KeycardChannelQtNfc::s_globalCardSession;
#endif

#if defined(Q_OS_ANDROID) || defined(ANDROID)
#include <QtCore/private/qjnihelpers_p.h>
#include <QJniObject>
#include <QJniEnvironment>
#include "keycard-qt/qt_nfc_android_workarounds.h"

// ============================================================================
// QT NFC ANDROID WORKAROUNDS - Global State
// ============================================================================
// This section contains workarounds for Qt 6.9.x NFC bugs on Android
// Controlled by ENABLE_QT_NFC_ANDROID_WORKAROUNDS CMake option
// ============================================================================

#if ENABLE_QT_NFC_ANDROID_WORKAROUNDS

// Forward declare JNI functions (defined in android_jni_register.cpp)
namespace Keycard {
bool registerQtNativeOnNewIntent();
bool manuallyStartNfcDiscovery();
}

namespace Keycard {

// Global state for Qt NFC workarounds
// These need to be in Keycard namespace to be accessible from android_jni_register.cpp

// Static flag to ensure registration happens only once
static bool g_jniRegistrationAttempted = false;

// Global callback for receiving NFC Intents directly from JNI
// WORKAROUND for Qt Bug #3: Broken signal emission
void (*g_nfcIntentCallback)(JNIEnv* env, jobject intent) = nullptr;

// Static reference to the active KeycardChannelQtNfc instance
static KeycardChannelQtNfc* g_activeKeycardChannel = nullptr;

// Global storage for the current Android NFC Tag
static QJniObject g_currentAndroidTag;

// Global storage for active IsoDep connection (kept alive for communication)
static QJniObject g_activeIsoDep;

} // namespace Keycard

// ============================================================================
// WORKAROUND FOR QT BUG #3: Direct Tag Processing and Signal Emission
// ============================================================================

// Static callback function called from JNI when NFC Intent arrives
void staticHandleNfcIntent(JNIEnv* env, jobject intent) {
    qWarning() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    qWarning() << "[Qt NFC Workaround] staticHandleNfcIntent() called from JNI!";
    
    if (!Keycard::g_activeKeycardChannel) {
        qCritical() << "No active KeycardChannelQtNfc to notify!";
        qWarning() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        return;
    }
    
    // Extract tag UID from Intent
    jclass qtNfcClass = env->FindClass("org/qtproject/qt/android/nfc/QtNfc");
    if (!qtNfcClass) {
        qCritical() << "Failed to find QtNfc class!";
        qWarning() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        return;
    }
    
    jmethodID getTagMethod = env->GetStaticMethodID(qtNfcClass, "getTag",
                                                      "(Landroid/content/Intent;)Landroid/os/Parcelable;");
    if (!getTagMethod) {
        qCritical() << "Failed to find QtNfc.getTag() method!";
        env->DeleteLocalRef(qtNfcClass);
        qWarning() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        return;
    }
    
    jobject tag = env->CallStaticObjectMethod(qtNfcClass, getTagMethod, intent);
    env->DeleteLocalRef(qtNfcClass);
    
    if (!tag) {
        qWarning() << "No NFC tag in Intent";
        qWarning() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        return;
    }
    
    
    // Store the tag globally for IsoDep communication
    Keycard::g_currentAndroidTag = QJniObject::fromLocalRef(tag);
    
    // Get tag UID
    QJniObject idArrayObj = Keycard::g_currentAndroidTag.callObjectMethod("getId", "()[B");
    jbyteArray idArray = idArrayObj.object<jbyteArray>();
    
    if (idArray && env) {
        jsize length = env->GetArrayLength(idArray);
        jbyte* idBytes = env->GetByteArrayElements(idArray, nullptr);
        
        QString uidHex;
        for (jsize i = 0; i < length; i++) {
            uidHex += QString::asprintf("%02x", (unsigned char)idBytes[i]);
        }
        
        env->ReleaseByteArrayElements(idArray, idBytes, JNI_ABORT);
        QJniObject isoDep = QJniObject::callStaticObjectMethod(
            "android/nfc/tech/IsoDep",
            "get",
            "(Landroid/nfc/Tag;)Landroid/nfc/tech/IsoDep;",
            Keycard::g_currentAndroidTag.object()
        );
        
        if (isoDep.isValid()) {
            // Check if already connected
            bool isConnected = isoDep.callMethod<jboolean>("isConnected");
            
            if (!isConnected) {
                isoDep.callMethod<void>("connect");
            } else {
                qWarning() << "IsoDep already connected - reusing existing connection";
            }
            
            // CRITICAL: Use 120-second timeout like React Native does!
            // EXPORT_KEY commands can take a long time, especially during login.
            // 5 seconds was causing response truncation.
            isoDep.callMethod<void>("setTimeout", "(I)V", 120000);
            
            // Check extended length APDU support
            bool supportsExtended = isoDep.callMethod<jboolean>("isExtendedLengthApduSupported");
            
            // Get max transceive length
            int maxLength = isoDep.callMethod<jint>("getMaxTransceiveLength");
            
            // Get historical bytes (might contain info about card capabilities)
            QJniObject historicalBytes = isoDep.callObjectMethod("getHistoricalBytes", "()[B");
            if (historicalBytes.isValid()) {
                qWarning() << "IsoDep has historical bytes";
            }
            
            // Store globally for later use
            Keycard::g_activeIsoDep = isoDep;
                        
            // Emit signal to SessionManager
            QMetaObject::invokeMethod(Keycard::g_activeKeycardChannel, [uidHex]() {
                emit Keycard::g_activeKeycardChannel->targetDetected(uidHex);
            }, Qt::QueuedConnection);
        } else {
            qWarning() << "Failed to get IsoDep from tag";
        }
    }
}

#endif // ENABLE_QT_NFC_ANDROID_WORKAROUNDS
#endif // Q_OS_ANDROID || ANDROID

namespace Keycard {

KeycardChannelQtNfc::KeycardChannelQtNfc(QObject* parent)
    : KeycardChannelBackend(parent)
    , m_manager(nullptr)  // Create after JNI registration
    , m_target(nullptr)
    , m_pollTimer(new QTimer(this))
    , m_pollingInterval(100)
{
    qDebug() << "KeycardChannelQtNfc: Initialized (iOS/Android)";
    qDebug() << "KeycardChannelQtNfc: Created in thread:" << QThread::currentThread();
    
#if defined(Q_OS_ANDROID) || defined(ANDROID)
#if ENABLE_QT_NFC_ANDROID_WORKAROUNDS
    // ╔══════════════════════════════════════════════════════════════╗
    // ║  QT NFC WORKAROUND #1: Register JNI Method                   ║
    // ║  Remove when Qt fixes QtNative.onNewIntent() registration    ║
    // ╚══════════════════════════════════════════════════════════════╝
    if (!Keycard::g_jniRegistrationAttempted) {
        Keycard::g_jniRegistrationAttempted = true;
        qDebug() << "[Qt NFC Workaround] Registering QtNative.onNewIntent()";
        bool success = registerQtNativeOnNewIntent();
        if (!success) {
            qCritical() << "[Qt NFC Workaround] JNI registration failed!";
        }
    }
    
    // ╔══════════════════════════════════════════════════════════════╗
    // ║  QT NFC WORKAROUND #3: Direct Intent Callback                ║
    // ║  Remove when Qt fixes targetDetected signal emission         ║
    // ╚══════════════════════════════════════════════════════════════╝
    Keycard::g_activeKeycardChannel = this;
    Keycard::g_nfcIntentCallback = &staticHandleNfcIntent;
    qDebug() << "[Qt NFC Workaround] Direct callback registered";
#else
    qDebug() << "[Qt NFC] Using standard Qt NFC (workarounds disabled)";
#endif // ENABLE_QT_NFC_ANDROID_WORKAROUNDS
#endif // Q_OS_ANDROID || ANDROID
    
    // Create QNearFieldManager AFTER JNI registration
    m_manager = new QNearFieldManager(this);
    qDebug() << "KeycardChannelQtNfc: QNearFieldManager created";
    
#if defined(Q_OS_IOS)
    // iOS: Check if global virtual session is being preserved from previous object
    if (s_globalCardSession.isValid()) {
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        qDebug() << "iOS: PRESERVING virtual session across object reconstruction!";
        qDebug() << "iOS: Card UID:" << s_globalCardSession.uid.toHex();
        qDebug() << "iOS: This prevents drawer open/close loops after login";
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    } else {
        qDebug() << "iOS: No existing virtual session (fresh start)";
    }
#endif
    
    // Connect signals
    connect(m_manager, &QNearFieldManager::targetDetected,
            this, &KeycardChannelQtNfc::onTargetDetected, Qt::DirectConnection);
    connect(m_manager, &QNearFieldManager::targetLost,
            this, &KeycardChannelQtNfc::onTargetLost, Qt::DirectConnection);
}

KeycardChannelQtNfc::~KeycardChannelQtNfc()
{
    stopDetection();
    disconnect();
#if defined(Q_OS_ANDROID) || defined(ANDROID)
#if ENABLE_QT_NFC_ANDROID_WORKAROUNDS
    // Clear global pointers on destruction
    Keycard::g_activeKeycardChannel = nullptr;
    Keycard::g_nfcIntentCallback = nullptr;
    Keycard::g_currentAndroidTag = QJniObject();
    Keycard::g_activeIsoDep = QJniObject();
#endif
#endif
}

void KeycardChannelQtNfc::startDetection()
{
    qDebug() << "========================================";
    qDebug() << "KeycardChannelQtNfc: Starting target detection";
    qDebug() << "KeycardChannelQtNfc: In thread:" << QThread::currentThread();
    qDebug() << "KeycardChannelQtNfc: Manager thread:" << m_manager->thread();
    
    bool nfcSupported = m_manager->isSupported();
    qDebug() << "KeycardChannelQtNfc: NFC supported:" << nfcSupported;
    
    if (!nfcSupported) {
        QString msg = "NFC not supported on this platform";
        qWarning() << "KeycardChannelQtNfc:" << msg;
        emit error(msg);
        return;
    }
    
#if defined(Q_OS_IOS)
    // iOS: NFC hardware is present, report as "virtual reader available"
    qDebug() << "iOS: NFC supported, reporting as available";
    
    // Idempotent: Only emit signal if detection wasn't already enabled
    if (!m_detectionEnabled) {
        m_detectionEnabled = true;
        emit readerAvailabilityChanged(true);
        qDebug() << "iOS: Emitted readerAvailabilityChanged(true)";
    } else {
        qDebug() << "iOS: Detection already enabled, not emitting signal again";
    }
    
    // iOS: Check if card is already virtually connected (from previous flow)
    // Session API (Login) needs this to work with existing virtual sessions
    // BUT: Don't emit if we're in the middle of auto-resume (physical target is null)
    // Auto-resume needs a PHYSICAL card tap, not a virtual session signal
    if (s_globalCardSession.isValid() && m_target) {
        // Virtual session exists AND physical connection is active
        // Safe to emit targetDetected for Session API
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        qDebug() << "iOS: Card already virtually+physically connected (UID:" << s_globalCardSession.uid.toHex() << ")";
        qDebug() << "iOS: Emitting targetDetected to notify Session API (reusing session)";
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        
        // Emit signals asynchronously to avoid re-entrancy
        QMetaObject::invokeMethod(this, [this]() {
            emit targetDetected(m_targetUid);
        }, Qt::QueuedConnection);
    } else if (!s_globalCardSession.isValid()) {
        // No virtual session at all
        // Don't automatically open the drawer here! It causes "ghost drawers" after login.
        // The drawer should only open when:
        // 1. User explicitly selects "Login with Keycard" (handled by SessionManager)
        // 2. A flow/operation needs the card (handled by transmit() auto-resume)
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        qDebug() << "iOS: No virtual session - detection enabled but drawer NOT opened";
        qDebug() << "iOS: Drawer will open when an operation actually needs the card";
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    } else {
        // Virtual session exists but physical target is null (auto-resume in progress)
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        qDebug() << "iOS: Virtual session exists but physical target null";
        qDebug() << "iOS: Physical card tap will trigger onTargetDetected()";
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    }
    
    qDebug() << "========================================";
    return;
#endif
    
    qWarning() << "🔧 Starting NFC target detection per Qt docs...";
    qWarning() << "🔧 Access method: QNearFieldTarget::TagTypeSpecificAccess";
    qWarning() << "🔧 Expected tag type: QNearFieldTarget::NfcTagType4 (Keycard is ISO 7816-4)";
    qWarning() << "🔧 CRITICAL: Keycard requires TagTypeSpecificAccess, not NdefAccess!";
    
    bool started = m_manager->startTargetDetection(QNearFieldTarget::TagTypeSpecificAccess);
    qWarning() << "🔧 startTargetDetection() returned:" << started;
    
#if defined(Q_OS_ANDROID) || defined(ANDROID)
#if ENABLE_QT_NFC_ANDROID_WORKAROUNDS
    // ╔══════════════════════════════════════════════════════════════╗
    // ║  QT NFC WORKAROUND #2: Enable Foreground Dispatch            ║
    // ║  Qt Bug: startTargetDetection() doesn't call startDiscovery()║
    // ║  Remove when Qt calls startDiscovery() automatically         ║
    // ╚══════════════════════════════════════════════════════════════╝
    qWarning() << "[Qt NFC Workaround] Manually calling QtNfc.startDiscovery()";
    if (!manuallyStartNfcDiscovery()) {
        qCritical() << "[Qt NFC Workaround] Failed to enable foreground dispatch!";
    }
#endif // ENABLE_QT_NFC_ANDROID_WORKAROUNDS
#endif // Q_OS_ANDROID || ANDROID
    
    qDebug() << "KeycardChannelQtNfc: Detection request completed";
    qDebug() << "========================================";
}

void KeycardChannelQtNfc::stopDetection()
{
    qDebug() << "KeycardChannelQtNfc: Stopping target detection";
    
#if defined(Q_OS_IOS)
    // iOS: Stop NFC session if it's active
    stopNfcSession();
    m_detectionEnabled = false;
    return;
#endif
    
    m_manager->stopTargetDetection();
#if defined(Q_OS_ANDROID) || defined(ANDROID)
#if ENABLE_QT_NFC_ANDROID_WORKAROUNDS
    // Also stop foreground dispatch if it was manually started
    qWarning() << "[Qt NFC Workaround] Manually calling QtNfc.stopDiscovery()";
    QMetaObject::invokeMethod(QCoreApplication::instance(), []() {
        QJniObject::callStaticMethod<void>("org/qtproject/qt/android/nfc/QtNfc", "stopDiscovery");
    }, Qt::QueuedConnection);
#endif
#endif
}

void KeycardChannelQtNfc::startNfcSession()
{
#if defined(Q_OS_IOS)
    if (!m_detectionEnabled) {
        startDetection();
        if (!m_detectionEnabled) {
            qWarning() << "iOS: Failed to start detection";
            return;
        }
    }
    
    if (m_nfcSessionActive) {
        qDebug() << "iOS: NFC session already active";
        return;
    }
    
    qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    qDebug() << "iOS: Starting NFC session (showing system drawer)";
    qDebug() << "iOS: QNearFieldManager supported:" << m_manager->isSupported();
    
    // Print callstack to trace what triggered drawer opening
    void* callstack[128];
    int frames = backtrace(callstack, 128);
    char** symbols = backtrace_symbols(callstack, frames);
    qDebug() << "iOS: Callstack for drawer open:";
    for (int i = 0; i < frames; ++i) {
        qDebug() << "  " << i << ":" << symbols[i];
    }
    free(symbols);
    
    qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    
    m_nfcSessionActive = true;
    
    // NOW trigger iOS CoreNFC UI
    // CRITICAL: Use TagTypeSpecificAccess for NfcTagType4 (Keycard is ISO 7816-4)
    // Qt Docs: "iOS supports NfcTagType4 with TagTypeSpecificAccess, not NdefAccess"
    // https://doc.qt.io/qt-6/qtnfc-features.html
    bool started = m_manager->startTargetDetection(QNearFieldTarget::TagTypeSpecificAccess);
    
    if (!started) {
        qCritical() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        qCritical() << "iOS: Failed to start NFC session with TagTypeSpecificAccess!";
        qCritical() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        qCritical() << "";
        qCritical() << "Keycard is NfcTagType4 (ISO 7816-4), requires TagTypeSpecificAccess";
        qCritical() << "";
        qCritical() << "POSSIBLE CAUSES:";
        qCritical() << "1. Info.plist missing: com.apple.developer.nfc.readersession.iso7816.select-identifiers";
        qCritical() << "2. Entitlements missing: com.apple.developer.nfc.readersession.formats";
        qCritical() << "3. Running in simulator (NFC only works on real device)";
        qCritical() << "4. Qt NFC iOS backend bug with TagTypeSpecificAccess";
        qCritical() << "";
        qCritical() << "NEXT STEPS:";
        qCritical() << "- Build app on physical iOS device (iPhone 7+)";
        qCritical() << "- Check Xcode logs for CoreNFC errors";
        qCritical() << "- Verify Info.plist AIDs are correctly included in build";
        qCritical() << "- If Qt NFC fails, implement native CoreNFC backend";
        qCritical() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        
        m_nfcSessionActive = false;
        emit error("Failed to start NFC session - see logs for details");
        return;
    }
    
    qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    qDebug() << "iOS: NFC session started successfully with TagTypeSpecificAccess!";
    qDebug() << "iOS: Configured for NfcTagType4 (ISO 7816-4 / Keycard)";
    qDebug() << "iOS: CoreNFC drawer should be visible to user";
    qDebug() << "iOS: Waiting for Keycard tap... onTargetDetected() will be called";
    qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
#else
    // Android/PCSC: Already detecting, this is a no-op
    qDebug() << "startNfcSession() called on non-iOS platform (no-op, already detecting)";
#endif
}

void KeycardChannelQtNfc::setState(ChannelState state)
{
    qDebug() << "KeycardChannelQtNfc: setState() called, transition:" << static_cast<int>(m_state) 
             << "→" << static_cast<int>(state);
    
    m_state = state;
    
#if defined(Q_OS_IOS)
    // iOS: Manage NFC session based on state
    switch (state) {
        case ChannelState::Idle:
            qDebug() << "iOS: State → Idle (dismissing any active NFC session)";
            if (m_nfcSessionActive) {
                stopNfcSession();
            }
            break;
            
        case ChannelState::WaitingForCard:
            qDebug() << "iOS: State → WaitingForCard (starting NFC session, showing drawer)";
            if (!m_nfcSessionActive) {
                startNfcSession();
            }
            break;
            
        case ChannelState::CardPresent:
            qDebug() << "iOS: State → CardPresent (keeping NFC session active)";
            // Keep session active for communication
            break;
            
        case ChannelState::UserInput:
            qDebug() << "iOS: State → UserInput (dismissing NFC drawer for input)";
            if (m_nfcSessionActive) {
                stopNfcSession();
            }
            break;
    }
#else
    // Android/PC/SC: Log state changes but no special handling needed
    Q_UNUSED(oldState);
    qDebug() << "State changed to:" << static_cast<int>(state) << "(no platform-specific action)";
#endif
}

void KeycardChannelQtNfc::stopNfcSession()
{
#if defined(Q_OS_IOS)
    if (!m_nfcSessionActive) {
        qDebug() << "iOS: NFC session not active, nothing to stop";
        return;
    }
    
    qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    qDebug() << "iOS: Stopping NFC session (dismissing drawer)";
    qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    
    // Disconnect from physical target to force iOS to close drawer immediately
    // But preserve logical session (card metadata) for next operation
    if (m_target) {
        qDebug() << "iOS: Disconnecting from physical target to dismiss drawer";
        // CRITICAL: Don't call disconnect() on m_target from background thread!
        // The target is a Qt object living on main thread, and calling disconnect()
        // from background thread causes Qt's NFC framework to access stale targets.
        // Just null out our pointer - Qt will handle target cleanup when iOS closes the session.
        s_globalCardSession.physicalTarget = nullptr;
        s_globalCardSession.physicallyConnected = false;
        m_target = nullptr;  // Release reference, drawer should dismiss now
    }
    
    qDebug() << "iOS: Logical session maintained (card metadata cached)";
    
    // CRITICAL: m_manager is a Qt object on main thread - marshal the call
    // Use BlockingQueuedConnection if on background thread to ensure drawer closes
    // Use DirectConnection if on main thread (already safe)
    QThread* mainThread = QCoreApplication::instance()->thread();
    QThread* currentThread = QThread::currentThread();
    
    if (currentThread == mainThread) {
        // On main thread - call directly
        qDebug() << "iOS: Stopping target detection (main thread, direct call)";
        m_manager->stopTargetDetection();
    } else {
        // Background thread - marshal and WAIT for completion
        qDebug() << "iOS: Stopping target detection (background thread, blocking call)";
        QMetaObject::invokeMethod(m_manager, [this]() {
            m_manager->stopTargetDetection();
        }, Qt::BlockingQueuedConnection);
    }
    
    m_nfcSessionActive = false;
    
    // Transition to Idle state so flows can detect that session was cancelled
    m_state = ChannelState::Idle;
#else
    // Android/PCSC: No-op
    qDebug() << "stopNfcSession() called on non-iOS platform (no-op)";
#endif
}

bool KeycardChannelQtNfc::requestCardAtStartup()
{
#if defined(Q_OS_IOS)
    qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    qDebug() << "iOS: requestCardAtStartup() - Non-blocking approach";
    qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    
    // If card already logically connected, we're good
    if (s_globalCardSession.isValid()) {
        qDebug() << "iOS: Card already logically connected (UID:" << s_globalCardSession.uid.toHex() << ")";
        return true;
    }
    
    // No card - open drawer but DON'T block
    // The flow will handle waiting for card detection via waitForCard()
    qDebug() << "iOS: No card connected, opening drawer (non-blocking)";
    
    if (!m_nfcSessionActive) {
        startNfcSession();
    }
    
    // Return immediately - flow will wait via pauseAndWait(INSERT_CARD)
    qDebug() << "iOS: Drawer opened, returning immediately (flow will handle waiting)";
    return true;
#else
    // Android/PC/SC: No-op - background detection already running
    qDebug() << "requestCardAtStartup() called on non-iOS platform - background detection active";
    return true;
#endif
}

void KeycardChannelQtNfc::disconnect()
{
    qDebug() << "KeycardChannelQtNfc: Disconnecting from target";
    
#if defined(Q_OS_IOS)
    // iOS: Only close physical session (drawer), keep logical session alive
    // This allows the virtual session model to work - card stays "inserted"
    // even though the physical NFC session (drawer) is closed
    qDebug() << "iOS: Closing physical session (keeping logical session for next flow)";
    stopNfcSession();
    
    // Don't clear s_globalCardSession - keep logical connection alive!
    // Don't emit cardRemoved - card is still virtually "inserted"
    
    qDebug() << "iOS: Virtual session maintained (card UID:" << s_globalCardSession.uid.toHex() << ")";
    qDebug() << "iOS: Next flow will auto-resume NFC session with same card";
#else
    
#if defined(Q_OS_ANDROID) || defined(ANDROID)
#if ENABLE_QT_NFC_ANDROID_WORKAROUNDS
    // Close IsoDep connection if it was opened
    if (Keycard::g_activeIsoDep.isValid()) {
        qWarning() << "[Qt NFC Workaround] Closing active IsoDep connection";
        Keycard::g_activeIsoDep.callMethod<void>("close");
        Keycard::g_activeIsoDep = QJniObject();
    }
#endif
#endif
    if (m_target) {
        m_target->disconnect();
        m_target->deleteLater();
        m_target = nullptr;
    }
    m_targetUid.clear();
    emit cardRemoved();
#endif
}

bool KeycardChannelQtNfc::isConnected() const
{
#if defined(Q_OS_ANDROID) || defined(ANDROID)
#if ENABLE_QT_NFC_ANDROID_WORKAROUNDS
    // For Android with workarounds, we directly manage IsoDep connection
    return Keycard::g_activeIsoDep.isValid();
#endif
#endif

#if defined(Q_OS_IOS)
    // iOS: Virtual session model - return logical connection state
    // Card is "connected" if we have its metadata, even if physical NFC session is closed
    // This allows UI dialogs to appear without losing connection
    return s_globalCardSession.isValid();
#endif
    
    return m_target != nullptr;
}

QByteArray KeycardChannelQtNfc::transmit(const QByteArray& apdu)
{
    // Track if we auto-resumed (opened drawer) for this specific operation
    // Used to decide whether to close drawer after getting response
    bool didAutoResume = false;
    
#if defined(Q_OS_IOS)
    // iOS: Check card session state and auto-resume if needed
    
    if (!s_globalCardSession.isValid()) {
        qWarning() << "iOS: No card detected yet - NFC session active, waiting for user to tap card";
        
        // Return error response instead of throwing
        QByteArray errorResponse;
        errorResponse.append(static_cast<char>(0x6A)); // SW1: Wrong parameters
        errorResponse.append(static_cast<char>(0x87)); // SW2: No card detected
        return errorResponse;
    }
    
    if (!m_target) {
        qDebug() << "iOS: Card logically connected but physical session closed (drawer was dismissed)";
        qDebug() << "iOS: Auto-resuming NFC session - user needs to tap the same card";
        
        didAutoResume = true;  // Mark that we're opening the drawer for this operation
        
        // CRITICAL: Restart detection first, then open drawer (marshal to main thread)
        // Detection was stopped after previous operation to prevent drawer pop-ups
        QMetaObject::invokeMethod(this, [this]() {
            qDebug() << "iOS: Restarting detection before opening drawer";
            startDetection();  // Enable NFC hardware listening
            setState(ChannelState::WaitingForCard);  // Open drawer
        }, Qt::QueuedConnection);
        
        // Wait up to 60 seconds for card to be re-tapped
        // This event loop runs in the background thread (where transmit() is called from)
        QEventLoop waitLoop;
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        timeoutTimer.setInterval(60000); // 60 seconds (iOS NFC session timeout)
        
        bool cardResumed = false;
        
        // Connect signals - card detection happens on main thread, signals us here
        connect(this, &KeycardChannelQtNfc::targetDetected, &waitLoop, [&waitLoop, &cardResumed]() {
            qDebug() << "iOS: Card re-detected, resuming operation";
            cardResumed = true;
            waitLoop.quit();
        }, Qt::QueuedConnection);
        
        connect(&timeoutTimer, &QTimer::timeout, &waitLoop, [&waitLoop]() {
            qDebug() << "iOS: Timeout waiting for card re-tap";
            waitLoop.quit();
        });
        
        // Start timeout timer
        timeoutTimer.start();
        
        // Wait for card or timeout
        qDebug() << "iOS: Waiting for user to re-tap the Keycard...";
        waitLoop.exec();
        
        // Clean up
        timeoutTimer.stop();
        QObject::disconnect(this, &KeycardChannelQtNfc::targetDetected, &waitLoop, nullptr);
        QObject::disconnect(&timeoutTimer, &QTimer::timeout, &waitLoop, nullptr);
        
        if (!cardResumed || !m_target) {
            qWarning() << "iOS: Card not re-tapped in time";
            
            // Return error response instead of throwing
            QByteArray errorResponse;
            errorResponse.append(static_cast<char>(0x6A)); // SW1: Wrong parameters
            errorResponse.append(static_cast<char>(0x88)); // SW2: Card not re-tapped
            return errorResponse;
        }
        
        qDebug() << "iOS: Physical session resumed, proceeding with operation";
    }
    
    qDebug() << "iOS: Card ready (UID:" << s_globalCardSession.uid.toHex() << "), transmitting APDU";
#endif
    
#if defined(Q_OS_ANDROID) || defined(ANDROID)
#if ENABLE_QT_NFC_ANDROID_WORKAROUNDS
    // ╔══════════════════════════════════════════════════════════════╗
    // ║  QT NFC WORKAROUND: Use IsoDep Directly                      ║
    // ║  Qt Bug: QNearFieldTarget doesn't work reliably              ║
    // ║  Remove when Qt NFC's sendCommand() works properly           ║
    // ╚══════════════════════════════════════════════════════════════╝
    if (Keycard::g_activeIsoDep.isValid()) {
        qDebug() << "KeycardChannelQtNfc: Transmitting APDU:" << apdu.toHex();
        qDebug() << "[Qt NFC Workaround] Using IsoDep directly";

        // Transceive APDU using QJniEnvironment
        QJniEnvironment jniEnv;
        jbyteArray apduArray = jniEnv->NewByteArray(apdu.size());
        jniEnv->SetByteArrayRegion(apduArray, 0, apdu.size(),
                                    reinterpret_cast<const jbyte*>(apdu.constData()));

        QJniObject responseArray = Keycard::g_activeIsoDep.callObjectMethod(
            "transceive",
            "([B)[B",
            apduArray
        );

        jniEnv->DeleteLocalRef(apduArray);

        if (!responseArray.isValid()) {
            qWarning() << "❌ IsoDep transceive failed!";
            Keycard::g_activeIsoDep.callMethod<void>("close");
            Keycard::g_activeIsoDep = QJniObject();
            
            // Return error response instead of throwing
            QByteArray errorResponse;
            errorResponse.append(static_cast<char>(0x6F)); // SW1: Command not allowed
            errorResponse.append(static_cast<char>(0x04)); // SW2: IsoDep transceive failed
            return errorResponse;
        }

        // Convert response to QByteArray
        jbyteArray jResponse = responseArray.object<jbyteArray>();
        jsize responseLength = jniEnv->GetArrayLength(jResponse);
        jbyte* responseBytes = jniEnv->GetByteArrayElements(jResponse, nullptr);

        QByteArray response((const char*)responseBytes, responseLength);

        jniEnv->ReleaseByteArrayElements(jResponse, responseBytes, JNI_ABORT);

        qDebug() << "KeycardChannelQtNfc: Received response:" << response.toHex();

        // Handle multi-frame responses (SW1=0x61 = more data available)
        QByteArray finalResponse = response;

        while (finalResponse.size() >= 2) {
            uint8_t sw1 = (finalResponse[finalResponse.size() - 2] & 0xFF);
            uint8_t sw2 = (finalResponse[finalResponse.size() - 1] & 0xFF);

            if (sw1 != 0x61) {
                break; // No more frames
            }

            qWarning() << "🔄 KeycardChannelQtNfc: Multi-frame response detected (SW1=0x61)";
            qWarning() << "🔄 Remaining bytes:" << (int)sw2;
            qWarning() << "🔄 Sending GET RESPONSE to retrieve additional data...";

            // Send GET RESPONSE command
            QByteArray getResponseApdu;
            getResponseApdu.append((char)0x00);  // CLA
            getResponseApdu.append((char)0xC0);  // INS (GET RESPONSE)
            getResponseApdu.append((char)0x00);  // P1
            getResponseApdu.append((char)0x00);  // P2
            getResponseApdu.append((char)sw2);   // Le (remaining bytes)

            // Send GET RESPONSE APDU
            jbyteArray getResponseArray = jniEnv->NewByteArray(getResponseApdu.size());
            jniEnv->SetByteArrayRegion(getResponseArray, 0, getResponseApdu.size(),
                                      reinterpret_cast<const jbyte*>(getResponseApdu.constData()));

            QJniObject additionalResponseArray = Keycard::g_activeIsoDep.callObjectMethod(
                "transceive",
                "([B)[B",
                getResponseArray
            );

            jniEnv->DeleteLocalRef(getResponseArray);

            if (additionalResponseArray.isValid()) {
                // Convert additional response to QByteArray
                jbyteArray jAdditionalResponse = additionalResponseArray.object<jbyteArray>();
                jsize additionalLength = jniEnv->GetArrayLength(jAdditionalResponse);
                jbyte* additionalBytes = jniEnv->GetByteArrayElements(jAdditionalResponse, nullptr);

                QByteArray additionalResponse((const char*)additionalBytes, additionalLength);
                jniEnv->ReleaseByteArrayElements(jAdditionalResponse, additionalBytes, JNI_ABORT);

                qDebug() << "🔄 KeycardChannelQtNfc: Received additional data:" << additionalResponse.toHex();

                // Combine responses (remove SW from current response, append new response)
                finalResponse = finalResponse.left(finalResponse.size() - 2) + additionalResponse;
                qDebug() << "🔄 KeycardChannelQtNfc: Combined response now:" << finalResponse.toHex();
            } else {
                qWarning() << "❌ GET RESPONSE failed!";
                break;
            }
        }

        return finalResponse;
    }
#endif // ENABLE_QT_NFC_ANDROID_WORKAROUNDS
#endif // Q_OS_ANDROID || ANDROID
    
    // Standard Qt NFC path (for iOS or when Android workarounds disabled)
    if (!m_target) {
        qWarning() << "KeycardChannelQtNfc: Not connected to any target";
        
        // Return error response instead of throwing
        QByteArray errorResponse;
        errorResponse.append(static_cast<char>(0x6A)); // SW1: Wrong parameters
        errorResponse.append(static_cast<char>(0x89)); // SW2: Not connected
        return errorResponse;
    }
    
    if (!isConnected()) {
        qWarning() << "KeycardChannelQtNfc: Target disconnected";
        
        // Return error response instead of throwing
        QByteArray errorResponse;
        errorResponse.append(static_cast<char>(0x6A)); // SW1: Wrong parameters
        errorResponse.append(static_cast<char>(0x8A)); // SW2: Target disconnected
        return errorResponse;
    }
    
    qDebug() << "KeycardChannelQtNfc: Transmitting APDU:" << apdu.toHex();
    
    // Check if target is valid before sending
    qDebug() << "KeycardChannelQtNfc: Target valid:" << (m_target != nullptr);
    if (m_target) {
        qDebug() << "KeycardChannelQtNfc: Target type:" << m_target->type();
        qDebug() << "KeycardChannelQtNfc: Target access methods:" << m_target->accessMethods();
    }
    
    // CRITICAL: sendCommand() must be called from main thread (where m_target lives)
    // But transmit() is called from background thread (flow execution)
    // Solution: Marshal to main thread, wait for result
    // Capture target pointer locally to avoid race condition with stopNfcSession()
    QNearFieldTarget* target = m_target;
    if (!target) {
        qWarning() << "KeycardChannelQtNfc: transmit() called with null target";
        return QByteArray();
    }
    
    QNearFieldTarget::RequestId requestId;
    bool sendSuccess = false;
    
    qDebug() << "KeycardChannelQtNfc: transmit() called from thread:" << QThread::currentThread();
    qDebug() << "KeycardChannelQtNfc: m_target lives in thread:" << target->thread();
    
    // Use QMetaObject::invokeMethod with BlockingQueuedConnection to safely call from background thread
    if (QThread::currentThread() == target->thread()) {
        // Already on correct thread - call directly
        qDebug() << "KeycardChannelQtNfc: On main thread, calling sendCommand directly";
        requestId = target->sendCommand(apdu);
        sendSuccess = true;
    } else {
        // Background thread - marshal to main thread
        qDebug() << "KeycardChannelQtNfc: On background thread, marshaling to main thread";
        QMetaObject::invokeMethod(this, [target, apdu, &requestId, &sendSuccess]() {
            qDebug() << "KeycardChannelQtNfc: [Lambda on main thread] calling sendCommand";
            requestId = target->sendCommand(apdu);
            sendSuccess = true;
            qDebug() << "KeycardChannelQtNfc: [Lambda on main thread] sendCommand returned";
        }, Qt::BlockingQueuedConnection);
    }
    
    qDebug() << "KeycardChannelQtNfc: sendCommand() returned, requestId valid:" << requestId.isValid();
    
    if (!sendSuccess || !requestId.isValid()) {
        qWarning() << "KeycardChannelQtNfc: Failed to send command - invalid request ID";
        
        // Return error response instead of throwing
        QByteArray errorResponse;
        errorResponse.append(static_cast<char>(0x6F)); // SW1: Command not allowed
        errorResponse.append(static_cast<char>(0x03)); // SW2: Invalid request ID
        return errorResponse;
    }
    
    // Create event loop to wait for response
    // This event loop runs in the BACKGROUND thread and waits for the response signal from main thread
    QEventLoop eventLoop;
    PendingRequest pending;
    pending.eventLoop = &eventLoop;
    pending.completed = false;
    
    m_pendingRequests.insert(requestId, pending);
    
    // Set timeout (iOS: no app-level timeout, rely on iOS 60s session timeout)
    QTimer timeout;
    timeout.setSingleShot(true);
    
#if defined(Q_OS_IOS)
    // iOS: No app-level timeout - user might need time to find their keycard
    // iOS NFC session has its own 60-second timeout which will close the drawer
    // When that happens, the request will fail naturally without needing our timeout
    timeout.setInterval(0);  // Disabled
    qDebug() << "iOS: No app-level timeout (relying on iOS 60s session timeout)";
#else
    // Android/PC/SC: 5-second timeout for APDU response
    timeout.setInterval(5000);
    connect(&timeout, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
    timeout.start();
    qDebug() << "Non-iOS: 5-second timeout set for APDU response";
#endif
    
    qDebug() << "KeycardChannelQtNfc: About to wait in event loop for response...";
    eventLoop.exec(); // Block until response or timeout (or iOS session expiry)
    qDebug() << "KeycardChannelQtNfc: Event loop exited";
    
#if !defined(Q_OS_IOS)
    // Android/PC/SC: Check if timeout occurred
    if (timeout.isActive()) {
        qDebug() << "KeycardChannelQtNfc: Timer still active - got response before timeout";
        timeout.stop();
    } else {
        // Timeout occurred - return error gracefully instead of throwing
        qWarning() << "KeycardChannelQtNfc: Command timeout after 5 seconds";
        m_pendingRequests.remove(requestId);
        
        // Return error response instead of throwing
        QByteArray errorResponse;
        errorResponse.append(static_cast<char>(0x6F)); // SW1: Command not allowed
        errorResponse.append(static_cast<char>(0x00)); // SW2: Timeout
        return errorResponse;
    }
#endif
    
    // Re-fetch pending in case it was modified
    auto it2 = m_pendingRequests.find(requestId);
    if (it2 == m_pendingRequests.end()) {
        qCritical() << "KeycardChannelQtNfc: Pending request was removed!";
        
        // Return error response instead of throwing
        QByteArray errorResponse;
        errorResponse.append(static_cast<char>(0x6F)); // SW1: Command not allowed
        errorResponse.append(static_cast<char>(0x01)); // SW2: Request removed
        return errorResponse;
    }
    
    qDebug() << "KeycardChannelQtNfc: Checking completion flag: pending.completed =" << it2->completed;
    if (!it2->completed) {
        qCritical() << "KeycardChannelQtNfc: completed flag is FALSE even though event loop exited!";
        m_pendingRequests.remove(requestId);
        
        // Return error response instead of throwing
        QByteArray errorResponse;
        errorResponse.append(static_cast<char>(0x6F)); // SW1: Command not allowed
        errorResponse.append(static_cast<char>(0x02)); // SW2: Command cancelled
        return errorResponse;
    }
    
    // Use it2 (the re-fetched iterator) not the old pending reference!
    QByteArray response = it2->response;
    m_pendingRequests.remove(requestId);
    
    qDebug() << "KeycardChannelQtNfc: Received response:" << response.toHex();
    
#if defined(Q_OS_IOS)
    // iOS: Keep drawer open for multi-step operations
    // Both Flow API and Session API will manage closing via their own logic
    // - Flow API: Uses state transitions (Idle/UserInput)
    // - Session API: Calls stopDetection() + setState(Idle) after authorize() completes
    // 
    // CRITICAL: Don't close here! reestablishSecureChannel() does multiple transmits:
    //   1. PAIR OPEN (transmit)
    //   2. MUTUALLY AUTHENTICATE (transmit)
    // If we close after step 1, the secure channel gets reset and step 2 crashes!
    if (didAutoResume) {
        qDebug() << "iOS: Auto-resumed operation complete, keeping drawer open for multi-step operations";
    } else {
        qDebug() << "iOS: Operation complete, keeping NFC session open for next operation";
    }
#endif
    
    return response;
}

void KeycardChannelQtNfc::onTargetDetected(QNearFieldTarget* target)
{    
    qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    qDebug() << "KeycardChannelQtNfc::onTargetDetected() CALLED!";
    qDebug() << "RUNNING IN THREAD:" << QThread::currentThread();
    qDebug() << "Main thread is:" << QCoreApplication::instance()->thread();
    qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    
    if (!target) {
        qWarning() << "KeycardChannelQtNfc: onTargetDetected called with null target";
        return;
    }
    
    QByteArray newUid = target->uid().toHex();
    
#if defined(Q_OS_IOS)
    // iOS: Virtual session model - handle logical vs physical connections
    QByteArray newUidBytes = target->uid();
    
    if (!s_globalCardSession.isValid()) {
        // First detection - establish logical session
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        qDebug() << "iOS: FIRST CARD DETECTION - establishing logical session";
        qDebug() << "iOS: Card UID:" << newUid;
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        
        s_globalCardSession.uid = newUidBytes;
        s_globalCardSession.logicallyConnected = true;
        s_globalCardSession.physicallyConnected = true;
        s_globalCardSession.physicalTarget = target;
        s_globalCardSession.lastSeen = QDateTime::currentDateTime();
        
        m_target = target;
        m_targetUid = newUid;
        
        qDebug() << "iOS: Logical session established, card stays 'connected'";
        
        // Don't emit signals here yet - defer to common code below
        // This avoids duplicate signal emission
        
    } else if (s_globalCardSession.uid == newUidBytes) {
        // Same card re-tapped - resume physical session
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        qDebug() << "iOS: SAME CARD RE-TAPPED - resuming physical session";
        qDebug() << "iOS: Card UID:" << newUid;
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        
        // Clean up old physical target if exists
        if (s_globalCardSession.physicalTarget && s_globalCardSession.physicalTarget != target) {
            s_globalCardSession.physicalTarget->disconnect();
            s_globalCardSession.physicalTarget->deleteLater();
        }
        
        s_globalCardSession.physicallyConnected = true;
        s_globalCardSession.physicalTarget = target;
        s_globalCardSession.lastSeen = QDateTime::currentDateTime();
        
        m_target = target;
        
        qDebug() << "iOS: Physical session resumed (logical session maintained)";
        
        // Set up Qt signal connections for this target (same as common code below)
        qDebug() << "KeycardChannelQtNfc: Setting up Qt signal connections for resumed target";
        
        // Connect to target signals (Qt 6 API)
        connect(m_target, &QNearFieldTarget::requestCompleted,
                this, [this](const QNearFieldTarget::RequestId& id) {
            qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
            qDebug() << "KeycardChannelQtNfc: requestCompleted signal received!";
            qDebug() << "KeycardChannelQtNfc: requestId valid:" << id.isValid();
            
            if (!m_target) {
                qWarning() << "KeycardChannelQtNfc: requestCompleted received but target is null - ignoring";
                return;
            }
            
            auto it = m_pendingRequests.find(id);
            if (it != m_pendingRequests.end()) {
                qDebug() << "KeycardChannelQtNfc: Found matching pending request";
                QVariant result = m_target->requestResponse(id);
                it->response = result.toByteArray();
                qDebug() << "KeycardChannelQtNfc: Got response, length:" << it->response.length() << "bytes";
                qDebug() << "KeycardChannelQtNfc: Setting completed = true";
                it->completed = true;
                qDebug() << "KeycardChannelQtNfc: completed flag is now:" << it->completed;
                if (it->eventLoop) {
                    qDebug() << "KeycardChannelQtNfc: Quitting event loop";
                    it->eventLoop->quit();
                } else {
                    qWarning() << "KeycardChannelQtNfc: No event loop to quit!";
                }
            } else {
                qWarning() << "KeycardChannelQtNfc: requestCompleted for unknown requestId!";
            }
            qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        }, Qt::DirectConnection);
        
        connect(m_target, &QNearFieldTarget::error,
                this, [this](QNearFieldTarget::Error error, const QNearFieldTarget::RequestId& id) {
            qCritical() << "KeycardChannelQtNfc: Target error signal:" << error << "for requestId:" << id.isValid();
            auto it = m_pendingRequests.find(id);
            if (it != m_pendingRequests.end()) {
                it->completed = false;
                qCritical() << "KeycardChannelQtNfc: Marking request as failed";
                if (it->eventLoop) {
                    it->eventLoop->quit();
                }
            }
        }, Qt::DirectConnection);

        emit targetDetected(m_targetUid);
        
        // Transition to CardPresent state
        m_state = ChannelState::CardPresent;
        qDebug() << "KeycardChannelQtNfc: State transitioned to CardPresent (card ready for communication)";
        qDebug() << "KeycardChannelQtNfc: onTargetDetected() completing (same-card re-tap handled)";
        
        return;
        
    } else {
        // Different card - this is a card swap error!
        qWarning() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        qWarning() << "iOS: CARD SWAP DETECTED!";
        qWarning() << "Expected UID:" << s_globalCardSession.uid.toHex();
        qWarning() << "Tapped UID:" << newUid;
        qWarning() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        
        // Clean up old session
        if (s_globalCardSession.physicalTarget) {
            s_globalCardSession.physicalTarget->disconnect();
            s_globalCardSession.physicalTarget->deleteLater();
        }
        
        // Establish new logical session with this card
        s_globalCardSession.uid = newUidBytes;
        s_globalCardSession.logicallyConnected = true;
        s_globalCardSession.physicallyConnected = true;
        s_globalCardSession.physicalTarget = target;
        s_globalCardSession.lastSeen = QDateTime::currentDateTime();
        
        m_target = target;
        m_targetUid = newUid;
        
        qWarning() << "iOS: New card session established (old session terminated)";
        
        emit error("Different Keycard detected. Please use the same Keycard or restart the operation.");
        emit targetDetected(m_targetUid);
    }
#else
    // Android/PC/SC: Standard behavior - reject if already connected
    if (m_target) {
        qWarning() << "KeycardChannelQtNfc: Already connected to a target, ignoring new detection";
        target->deleteLater();
        return;
    }
    
    m_target = target;
    m_targetUid = newUid;
    
    qDebug() << "KeycardChannelQtNfc: Target detected - UID:" << m_targetUid;
    qDebug() << "KeycardChannelQtNfc: Target type:" << target->type();
#endif
    
    qDebug() << "KeycardChannelQtNfc: Setting up Qt signal connections for target";
    
    // Connect to target signals (Qt 6 API)
    // Note: commandSent/responseReceived removed in Qt 6, use requestCompleted instead
    connect(m_target, &QNearFieldTarget::requestCompleted,
            this, [this](const QNearFieldTarget::RequestId& id) {
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        qDebug() << "KeycardChannelQtNfc: requestCompleted signal received!";
        qDebug() << "KeycardChannelQtNfc: requestId valid:" << id.isValid();
        
        // Check if target is still valid (might have been disconnected)
        if (!m_target) {
            qWarning() << "KeycardChannelQtNfc: requestCompleted received but target is null - ignoring";
            return;
        }
        
        auto it = m_pendingRequests.find(id);
        if (it != m_pendingRequests.end()) {
            qDebug() << "KeycardChannelQtNfc: Found matching pending request";
            QVariant result = m_target->requestResponse(id);
            it->response = result.toByteArray();
            qDebug() << "KeycardChannelQtNfc: Got response, length:" << it->response.length() << "bytes";
            qDebug() << "KeycardChannelQtNfc: Setting completed = true";
            it->completed = true;
            qDebug() << "KeycardChannelQtNfc: completed flag is now:" << it->completed;
            if (it->eventLoop) {
                qDebug() << "KeycardChannelQtNfc: Quitting event loop";
                it->eventLoop->quit();
            } else {
                qWarning() << "KeycardChannelQtNfc: No event loop to quit!";
            }
        } else {
            qWarning() << "KeycardChannelQtNfc: requestCompleted for unknown requestId!";
        }
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    }, Qt::DirectConnection);
    
    connect(m_target, &QNearFieldTarget::error,
            this, [this](QNearFieldTarget::Error error, const QNearFieldTarget::RequestId& id) {
        qCritical() << "KeycardChannelQtNfc: Target error signal:" << error << "for requestId:" << id.isValid();
        auto it = m_pendingRequests.find(id);
        if (it != m_pendingRequests.end()) {
            it->completed = false;
            qCritical() << "KeycardChannelQtNfc: Marking request as failed";
            if (it->eventLoop) {
                it->eventLoop->quit();
            }
        }
    }, Qt::DirectConnection);
    
    connect(m_target, &QNearFieldTarget::disconnected,
            this, [this]() {
        qDebug() << "KeycardChannelQtNfc: Target disconnected";
#if !defined(Q_OS_IOS)
        // Non-iOS: Clear connection state
        m_target = nullptr;
        m_targetUid.clear();
        emit cardRemoved();
#else
        // iOS: Keep card state persistent - card appears "always connected"
        Q_UNUSED(this);  // Suppress unused capture warning on iOS
        qDebug() << "iOS: Target disconnected but keeping card state (persistent connection model)";
#endif
    }, Qt::DirectConnection);
    
    // Emit signals directly - no blocking, so no deadlock risk
    qDebug() << "KeycardChannelQtNfc: Emitting targetDetected";
    emit targetDetected(m_targetUid);
    qDebug() << "KeycardChannelQtNfc: Signals emitted";
    
    // Transition to CardPresent state
    m_state = ChannelState::CardPresent;
    qDebug() << "KeycardChannelQtNfc: State transitioned to CardPresent (card ready for communication)";
    qDebug() << "KeycardChannelQtNfc: onTargetDetected() completing";
    
#if defined(Q_OS_IOS)
    // iOS: Keep NFC session open for communication
    // The session will be stopped when:
    // - disconnect() is called
    // - SessionManager closes the session
    // - iOS system timeout (60s)
    qDebug() << "iOS: Card detected, keeping NFC session open for communication";
#endif
}

void KeycardChannelQtNfc::onTargetLost(QNearFieldTarget* target)
{
#if defined(Q_OS_IOS)
    // iOS: Virtual session model - physical target lost but logical session persists
    if (s_globalCardSession.physicalTarget == target) {
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        qDebug() << "iOS: Physical target lost (NFC session ended)";
        qDebug() << "iOS: Logical session maintained - card still 'connected'";
        qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
        
        // Mark physical session as disconnected but keep logical session
        s_globalCardSession.physicallyConnected = false;
        s_globalCardSession.physicalTarget = nullptr;
        m_target = nullptr;
        
        // Do NOT emit cardRemoved - card is still logically connected
        // The card will be resumed on next transmit()
        qDebug() << "iOS: Card remains logically connected for next operation";
    } else {
        qDebug() << "iOS: Lost a non-active target";
        if (target) {
            target->deleteLater();
        }
    }
#else
    // Android/PCSC: Standard behavior - emit cardRemoved
    if (m_target == target) {
        qDebug() << "KeycardChannelQtNfc: Active target lost - UID:" << m_targetUid;
        disconnect();
    } else {
        qWarning() << "KeycardChannelQtNfc: Lost a non-active target";
        target->deleteLater();
    }
#endif
}

void KeycardChannelQtNfc::onCommandSent(QNearFieldTarget::RequestId id, const QByteArray& command)
{
    Q_UNUSED(command);
    Q_UNUSED(id);
    qDebug() << "KeycardChannelQtNfc: Command sent";
}

void KeycardChannelQtNfc::onResponseReceived(QNearFieldTarget::RequestId id, const QByteArray& response)
{
    qDebug() << "KeycardChannelQtNfc: Response received";
    auto it = m_pendingRequests.find(id);
    if (it != m_pendingRequests.end()) {
        it->response = response;
        it->completed = true;
        it->eventLoop->quit();
    } else {
        qWarning() << "KeycardChannelQtNfc: Received response for unknown request ID";
    }
}

void KeycardChannelQtNfc::onTargetError(QNearFieldTarget::Error error, QNearFieldTarget::RequestId id)
{
    qWarning() << "KeycardChannelQtNfc: Target error:" << error;
    auto it = m_pendingRequests.find(id);
    if (it != m_pendingRequests.end()) {
        it->error = error;
        it->eventLoop->quit();
    } else {
        qWarning() << "KeycardChannelQtNfc: Error for unknown request ID";
    }
    emit this->error(QString("NFC Target Error: %1").arg(error));
}

bool KeycardChannelQtNfc::waitForCardTap(int timeoutMs)
{
#if defined(Q_OS_IOS)
    qDebug() << "iOS: waitForCardTap() - waiting" << timeoutMs << "ms for card";
    
    QEventLoop waitLoop;
    
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setInterval(timeoutMs);
    connect(&timeoutTimer, &QTimer::timeout, &waitLoop, &QEventLoop::quit);
    
    bool cardDetected = false;
    QMetaObject::Connection cardConnection = connect(this, &KeycardChannelQtNfc::targetDetected, 
        [&waitLoop, &cardDetected](const QString& uid) {
            qDebug() << "iOS: waitForCardTap() - targetDetected signal received! UID:" << uid;
            cardDetected = true;
            waitLoop.quit();
        });
    
    timeoutTimer.start();
    waitLoop.exec();
    
    QObject::disconnect(cardConnection);
    timeoutTimer.stop();
    
    if (cardDetected) {
        qDebug() << "iOS: Card detected successfully";
        return true;
    } else {
        qWarning() << "iOS: Card tap timeout";
        return false;
    }
#else
    // Android/PCSC: Not needed
    Q_UNUSED(timeoutMs);
    return true;
#endif
}

void KeycardChannelQtNfc::onTargetDisconnected()
{
    qDebug() << "KeycardChannelQtNfc: onTargetDisconnected called";
    // This signal is emitted by QNearFieldTarget when it disconnects
    // No need to emit cardRemoved here, it's handled by disconnect()
}

} // namespace Keycard

