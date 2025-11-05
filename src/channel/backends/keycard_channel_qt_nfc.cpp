// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#include "keycard-qt/backends/keycard_channel_qt_nfc.h"
#include <QDebug>
#include <QThread>
#include <QCoreApplication>
#include <stdexcept>

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
                emit Keycard::g_activeKeycardChannel->cardDetected(uidHex);
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
    
    qWarning() << "🔧 Starting NFC target detection per Qt docs...";
    qWarning() << "🔧 Access method: QNearFieldTarget::NdefAccess";
    qWarning() << "🔧 Expected tag type: QNearFieldTarget::NfcTagType4 (Keycard)";
    
    bool started = m_manager->startTargetDetection(QNearFieldTarget::NdefAccess);
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

void KeycardChannelQtNfc::disconnect()
{
    qDebug() << "KeycardChannelQtNfc: Disconnecting from target";
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
}

bool KeycardChannelQtNfc::isConnected() const
{
#if defined(Q_OS_ANDROID) || defined(ANDROID)
#if ENABLE_QT_NFC_ANDROID_WORKAROUNDS
    // For Android with workarounds, we directly manage IsoDep connection
    return Keycard::g_activeIsoDep.isValid();
#endif
#endif
    return m_target != nullptr;
}

QByteArray KeycardChannelQtNfc::transmit(const QByteArray& apdu)
{
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
            throw std::runtime_error("IsoDep transceive failed");
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
        throw std::runtime_error("Not connected to any target");
    }
    
    if (!isConnected()) {
        throw std::runtime_error("Target disconnected");
    }
    
    qDebug() << "KeycardChannelQtNfc: Transmitting APDU:" << apdu.toHex();
    
    QNearFieldTarget::RequestId requestId = m_target->sendCommand(apdu);
    
    if (!requestId.isValid()) {
        throw std::runtime_error("Failed to send command - invalid request ID");
    }
    
    // Create event loop to wait for response
    QEventLoop eventLoop;
    PendingRequest pending;
    pending.eventLoop = &eventLoop;
    pending.completed = false;
    
    m_pendingRequests.insert(requestId, pending);
    
    // Set timeout
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(5000);
    connect(&timeout, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
    timeout.start();
    
    eventLoop.exec(); // Block until response or timeout
    
    if (timeout.isActive()) {
        timeout.stop();
    } else {
        // Timeout occurred
        m_pendingRequests.remove(requestId);
        throw std::runtime_error("NFC command timed out");
    }
    
    if (!pending.completed) {
        m_pendingRequests.remove(requestId);
        throw std::runtime_error("NFC command failed or was cancelled");
    }
    
    QByteArray response = pending.response;
    m_pendingRequests.remove(requestId);
    
    qDebug() << "KeycardChannelQtNfc: Received response:" << response.toHex();
    return response;
}

void KeycardChannelQtNfc::onTargetDetected(QNearFieldTarget* target)
{    
    if (!target) {
        qWarning() << "KeycardChannelQtNfc: onTargetDetected called with null target";
        return;
    }
    
    if (m_target) {
        qWarning() << "KeycardChannelQtNfc: Already connected to a target, ignoring new detection";
        target->deleteLater();
        return;
    }
    
    m_target = target;
    m_targetUid = target->uid().toHex();
    
    qDebug() << "KeycardChannelQtNfc: Target detected - UID:" << m_targetUid;
    qDebug() << "KeycardChannelQtNfc: Target type:" << target->type();
    
    // Connect to target signals (Qt 6 API)
    // Note: commandSent/responseReceived removed in Qt 6, use requestCompleted instead
    connect(m_target, &QNearFieldTarget::requestCompleted,
            this, [this](const QNearFieldTarget::RequestId& id) {
        auto it = m_pendingRequests.find(id);
        if (it != m_pendingRequests.end()) {
            QVariant result = m_target->requestResponse(id);
            it->response = result.toByteArray();
            it->completed = true;
            if (it->eventLoop) {
                it->eventLoop->quit();
            }
        }
    }, Qt::DirectConnection);
    
    connect(m_target, &QNearFieldTarget::disconnected,
            this, [this]() {
        qDebug() << "KeycardChannelQtNfc: Target disconnected";
        m_target = nullptr;
        m_targetUid.clear();
        emit cardRemoved();
    }, Qt::DirectConnection);
    
    emit targetDetected(m_targetUid);
    emit cardDetected(m_targetUid);  // Legacy signal
}

void KeycardChannelQtNfc::onTargetLost(QNearFieldTarget* target)
{
    if (m_target == target) {
        qDebug() << "KeycardChannelQtNfc: Active target lost - UID:" << m_targetUid;
        disconnect();
    } else {
        qWarning() << "KeycardChannelQtNfc: Lost a non-active target";
        target->deleteLater();
    }
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

void KeycardChannelQtNfc::onTargetDisconnected()
{
    qDebug() << "KeycardChannelQtNfc: onTargetDisconnected called";
    // This signal is emitted by QNearFieldTarget when it disconnects
    // No need to emit cardRemoved here, it's handled by disconnect()
}

} // namespace Keycard

