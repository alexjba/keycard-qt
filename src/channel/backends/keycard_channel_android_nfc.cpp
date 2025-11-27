#include "keycard-qt/backends/keycard_channel_android_nfc.h"
#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QThread>
#include <QtCore/private/qandroidextras_p.h>

namespace Keycard {

// Custom events for thread-safe signal emission from JNI callbacks
class TagConnectedEvent : public QEvent {
public:
    static const QEvent::Type EventType;
    QString uid;
    
    explicit TagConnectedEvent(const QString& u)
        : QEvent(EventType), uid(u) {}
};

class TagDisconnectedEvent : public QEvent {
public:
    static const QEvent::Type EventType;
    
    explicit TagDisconnectedEvent()
        : QEvent(EventType) {}
};

const QEvent::Type TagConnectedEvent::EventType = static_cast<QEvent::Type>(QEvent::User + 1);
const QEvent::Type TagDisconnectedEvent::EventType = static_cast<QEvent::Type>(QEvent::User + 2);

// Static members
QJniObject KeycardChannelAndroidNfc::s_activeIsoDep;
bool KeycardChannelAndroidNfc::s_connected = false;
QString KeycardChannelAndroidNfc::s_currentCardUID;

// Global callback for NFC intents
static KeycardChannelAndroidNfc* s_activeAndroidNfcBackend = nullptr;

// Forward declaration for JNI registration
static void registerJniMethods();

KeycardChannelAndroidNfc::KeycardChannelAndroidNfc(QObject* parent)
    : KeycardChannelBackend(parent)
{
    qDebug() << "KeycardChannelAndroidNfc::KeycardChannelAndroidNfc()";

    // Register this instance as the active backend
    s_activeAndroidNfcBackend = this;

    // Register JNI native methods for KeycardNfcReader callbacks
    registerJniMethods();

    // Get SINGLETON KeycardNfcReader instance
    m_readerCallback = QJniObject::callStaticObjectMethod(
        "im/status/keycard/android/KeycardNfcReader",
        "getInstance",
        "()Lim/status/keycard/android/KeycardNfcReader;"
    );

    if (!m_readerCallback.isValid()) {
        qWarning() << "KeycardChannelAndroidNfc: Could not get singleton KeycardNfcReader";
        return;
    }
}

static void registerJniMethods()
{
    JNINativeMethod methods[] = {
        {
            const_cast<char*>("onNativeTagConnected"),
            const_cast<char*>("(JLjava/lang/Object;)V"),
            reinterpret_cast<void*>(KeycardChannelAndroidNfc::onJavaTagConnected)
        },
        {
            const_cast<char*>("onNativeTagDisconnected"),
            const_cast<char*>("(J)V"),
            reinterpret_cast<void*>(KeycardChannelAndroidNfc::onJavaTagDisconnected)
        }
    };

    QJniEnvironment env;
    jclass clazz = env->FindClass("im/status/keycard/android/KeycardNfcReader");
    if (!clazz) {
        qWarning() << "KeycardChannelAndroidNfc: Could not find KeycardNfcReader class";
        return;
    }

    if (env->RegisterNatives(clazz, methods, 2) < 0) {
        qWarning() << "KeycardChannelAndroidNfc: Failed to register JNI native methods";
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        env->DeleteLocalRef(clazz);
        return;
    }

    env->DeleteLocalRef(clazz);
}

KeycardChannelAndroidNfc::~KeycardChannelAndroidNfc()
{
    qDebug() << "KeycardChannelAndroidNfc::~KeycardChannelAndroidNfc()";

    // SINGLETON ARCHITECTURE:
    // - Global singleton KeycardNfcReader stays alive
    // - Reader mode stays enabled for the app's lifetime
    // - Unregister backend if still registered (in case stopDetection() wasn't called)
    
    // 1. Ensure backend is unregistered (idempotent - safe to call even if not registered)
    if (m_readerCallback.isValid()) {
        jlong nativePtr = reinterpret_cast<jlong>(this);
        try {
            m_readerCallback.callMethod<void>("unregisterBackend", "(J)V", nativePtr);
        } catch (...) {
            // Ignore exceptions
        }
    }
    
    // 2. Clear our reference to the singleton (but don't destroy it!)
    // The singleton lives on for other backends
    m_readerCallback = QJniObject();
    
    s_activeIsoDep = QJniObject();
    s_connected = false;
    
    // 5. Unregister this instance
    if (s_activeAndroidNfcBackend == this) {
        s_activeAndroidNfcBackend = nullptr;
    }
}

bool KeycardChannelAndroidNfc::event(QEvent* e)
{
    // Handle custom events for thread-safe signal emission from JNI callbacks
    if (e->type() == TagConnectedEvent::EventType) {
        TagConnectedEvent* tce = static_cast<TagConnectedEvent*>(e);
        emit targetDetected(tce->uid);
        return true;
    } else if (e->type() == TagDisconnectedEvent::EventType) {
        emit cardRemoved();
        return true;
    }
    
    return KeycardChannelBackend::event(e);
}

bool KeycardChannelAndroidNfc::isAvailable() const
{
    // Check NFC availability via Android NFC API
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;"
    );

    if (!context.isValid()) {
        return false;
    }

    QJniObject nfcAdapter = QJniObject::callStaticObjectMethod(
        "android/nfc/NfcAdapter",
        "getDefaultAdapter",
        "(Landroid/content/Context;)Landroid/nfc/NfcAdapter;",
        context.object<jobject>()
    );

    if (!nfcAdapter.isValid()) {
        return false; // NFC not available on this device
    }

    jboolean nfcEnabled = nfcAdapter.callMethod<jboolean>("isEnabled");
    return nfcEnabled;
}

void KeycardChannelAndroidNfc::startDetection()
{
    qDebug() << "KeycardChannelAndroidNfc::startDetection()";

    if (!isAvailable()) {
        qWarning() << "KeycardChannelAndroidNfc: NFC not available";
        return;
    }

    if (!m_readerCallback.isValid()) {
        qWarning() << "KeycardChannelAndroidNfc: Singleton not available";
        return;
    }

    // Enable reader mode
    // Pass Activity from Qt (Java will register lifecycle callbacks on first call)
    try {
        auto context = QNativeInterface::QAndroidApplication::context();
        QJniObject activity = context.isValid() ? QJniObject(context) : QJniObject();
        
        if (activity.isValid()) {
            m_readerCallback.callMethod<void>("enableReaderMode", "(Landroid/app/Activity;)V", activity.object());
        } else {
            m_readerCallback.callMethod<void>("enableReaderMode", "(Landroid/app/Activity;)V", nullptr);
        }
    } catch (...) {
        qWarning() << "KeycardChannelAndroidNfc: Exception enabling reader mode";
    }

    // Register this backend with the singleton to START receiving NFC events
    jlong nativePtr = reinterpret_cast<jlong>(this);
    
    try {
        m_readerCallback.callMethod<void>("registerBackend", "(J)V", nativePtr);
    } catch (...) {
        qWarning() << "KeycardChannelAndroidNfc: Exception calling registerBackend()";
    }
}

void KeycardChannelAndroidNfc::stopDetection()
{
    qDebug() << "KeycardChannelAndroidNfc::stopDetection()";

    if (!m_readerCallback.isValid()) {
        return;
    }

    // Unregister this backend from the singleton to STOP receiving NFC events
    jlong nativePtr = reinterpret_cast<jlong>(this);
    
    try {
        m_readerCallback.callMethod<void>("unregisterBackend", "(J)V", nativePtr);
    } catch (...) {
        // Ignore exceptions
    }
    
    // Clear virtual session state (allow re-detection of same card when we restart)
    s_currentCardUID.clear();
}

void KeycardChannelAndroidNfc::disconnect()
{
    qDebug() << "KeycardChannelAndroidNfc::disconnect()";

    if (s_connected && m_readerCallback.isValid()) {
        try {
            // Call Java KeycardNfcReader.disconnect() which:
            // 1. Closes IsoDep connection
            // 2. Stops tag presence monitoring
            // 3. Disables reader mode (allows fresh detection on forceScan())
            m_readerCallback.callMethod<void>("disconnect", "()V");
        } catch (const std::exception& e) {
            qWarning() << "KeycardChannelAndroidNfc: Error calling disconnect():" << e.what();
        } catch (...) {
            qWarning() << "KeycardChannelAndroidNfc: Unknown error calling disconnect()";
        }
        
        s_activeIsoDep = QJniObject();
        s_connected = false;
        
        // DON'T clear s_currentCardUID here - matches iOS behavior!
        // iOS keeps virtual session across disconnect() to enable auto-recovery.
        // Virtual session is only cleared on physical removal or stopDetection().
        
        // DON'T emit cardRemoved() here!
        // cardRemoved should only be emitted when the card is physically removed
        // (detected by monitoring thread), not during programmatic disconnect
    }
}

void KeycardChannelAndroidNfc::forceScan()
{
    qDebug() << "KeycardChannelAndroidNfc::forceScan()";
    
    // Clear virtual session state to allow re-detection of the same card
    s_currentCardUID.clear();
    
    // Call Java singleton to manually trigger a new tag detection event
    // This is needed because if the card is still in the NFC field,
    // Android won't automatically fire onTagDiscovered again
    if (m_readerCallback.isValid()) {
        try {
            m_readerCallback.callMethod<void>("forceScan", "()V");
        } catch (...) {
            qWarning() << "KeycardChannelAndroidNfc: Exception calling Java forceScan()";
        }
    } else {
        qWarning() << "KeycardChannelAndroidNfc: Singleton not available, cannot trigger manual re-detection";
    }
}

void KeycardChannelAndroidNfc::setState(ChannelState state)
{
    qDebug() << "KeycardChannelAndroidNfc::setState() state:" << static_cast<int>(state);
    
    ChannelState oldState = m_state;
    m_state = state;
    
    // Android: Handle state transitions that require connection management
    switch (state) {
        case ChannelState::UserInput:
            // User input required (e.g., entering mnemonic, PIN in UI)
            // Disconnect card so OS timeout doesn't fire unexpected tagDisconnected()
            // Card will be re-detected when user completes input and flow resumes
            if (isConnected()) {
                disconnect();
            }
            break;
            
        case ChannelState::Idle:
            // Flow completed or cancelled
            // Disconnect and stop detection to save battery
            if (isConnected()) {
                disconnect();
            }
            break;
            
        case ChannelState::WaitingForCard:
            // Flow waiting for card (re)detection
            // Ensure reader mode is active
            if (!isConnected()) {
                startDetection();
            }
            break;
            
        case ChannelState::CardPresent:
            // Card is present and connected
            // No action needed - keep connection active
            break;
    }
}

QString KeycardChannelAndroidNfc::backendName() const
{
    return "Android NFC";
}

QByteArray KeycardChannelAndroidNfc::transmit(const QByteArray& apdu)
{
    // Check IsoDep connection
    if (!s_connected || !s_activeIsoDep.isValid()) {
        qWarning() << "KeycardChannelAndroidNfc: Not connected to IsoDep after forceScan";
        // Return error APDU or handle failure
        QByteArray errorResponse;
        errorResponse.append(static_cast<char>(0x6A));
        errorResponse.append(static_cast<char>(0x87)); // Card not detected
        return errorResponse;
    }

    qDebug() << "KeycardChannelAndroidNfc::transmit() size:" << apdu.size();

    try {
        QJniEnvironment env;

        // Convert APDU to Java byte array
        jbyteArray apduArray = env->NewByteArray(apdu.size());
        env->SetByteArrayRegion(apduArray, 0, apdu.size(),
                               reinterpret_cast<const jbyte*>(apdu.constData()));

        // Call transceive via our KeycardNfcReader (which calls IsoDep.transceive)
        QJniObject responseArray = m_readerCallback.callObjectMethod(
            "transceive",
            "([B)[B",
            apduArray
        );

        env->DeleteLocalRef(apduArray);

        if (!responseArray.isValid()) {
            qWarning() << "KeycardChannelAndroidNfc: Transceive failed (card may have been removed)";
            // s_activeIsoDep.callMethod<void>("close");
            s_activeIsoDep = QJniObject();
            s_connected = false;
            return QByteArray();
        }

        // Convert response back to QByteArray
        jbyteArray jResponse = responseArray.object<jbyteArray>();
        jsize responseLength = env->GetArrayLength(jResponse);
        jbyte* responseBytes = env->GetByteArrayElements(jResponse, nullptr);

        QByteArray response((const char*)responseBytes, responseLength);
        env->ReleaseByteArrayElements(jResponse, responseBytes, JNI_ABORT);

        qDebug() << "KeycardChannelAndroidNfc: Received response (" << response.size() << " bytes):" << response.toHex();

        // Handle multi-frame responses
        handleMultiFrameResponse(response);

        return response;

    } catch (const std::exception& e) {
        qWarning() << "KeycardChannelAndroidNfc: Transmit exception:" << e.what();
        return QByteArray();
    }
}

bool KeycardChannelAndroidNfc::isConnected() const
{
    return s_connected && s_activeIsoDep.isValid() &&
           s_activeIsoDep.callMethod<jboolean>("isConnected");
}

void KeycardChannelAndroidNfc::onTagDiscovered(const QJniObject& tag)
{
    qDebug() << "KeycardChannelAndroidNfc: Tag discovered";

    // if (s_connected) {
    //     qDebug() << "KeycardChannelAndroidNfc: Already connected, ignoring new tag";
    //     return;
    // }

    connectToIsoDep(tag);
}

// Static method to check for NFC intents
// This should be called from the main activity when new intents arrive
bool KeycardChannelAndroidNfc::checkForNfcIntent(const QJniObject& intent)
{
    if (!intent.isValid() || !s_activeAndroidNfcBackend) {
        return false;
    }

    // Check if this is an NFC intent
    QJniObject action = intent.callObjectMethod("getAction", "()Ljava/lang/String;");
    if (!action.isValid()) {
        return false;
    }

    QString actionStr = action.toString();
    qDebug() << "KeycardChannelAndroidNfc: Checking intent action:" << actionStr;

    // Check for NFC tag discovery actions
    if (actionStr == "android.nfc.action.TAG_DISCOVERED" ||
        actionStr == "android.nfc.action.TECH_DISCOVERED") {

        qDebug() << "KeycardChannelAndroidNfc: NFC tag intent detected!";

        // Extract the tag from the intent
        QJniObject tag = intent.callObjectMethod("getParcelableExtra",
                                                "(Ljava/lang/String;)Landroid/os/Parcelable;",
                                                QJniObject::fromString("android.nfc.extra.TAG").object<jstring>());

        if (tag.isValid()) {
            qDebug() << "KeycardChannelAndroidNfc: Tag extracted from intent";
            // Notify the active backend
            s_activeAndroidNfcBackend->onTagDiscovered(tag);
            return true;
        } else {
            qWarning() << "KeycardChannelAndroidNfc: Could not extract tag from NFC intent";
        }
    }

    return false;
}

// REMOVED: setupNfcAdapter()
// No longer needed with SINGLETON architecture - the singleton handles NFC adapter access

void KeycardChannelAndroidNfc::connectToIsoDep(const QJniObject& tag)
{
    qDebug() << "KeycardChannelAndroidNfc: Connecting to IsoDep";

    // Get IsoDep technology from tag
    QJniObject isoDep = QJniObject::callStaticObjectMethod(
        "android/nfc/tech/IsoDep",
        "get",
        "(Landroid/nfc/Tag;)Landroid/nfc/tech/IsoDep;",
        tag.object<jobject>()
    );

    if (!isoDep.isValid()) {
        qWarning() << "KeycardChannelAndroidNfc: Tag does not support IsoDep";
        return;
    }

    try {
        // Store the connection
        s_activeIsoDep = isoDep;
        s_connected = true;

        // Get tag UID for identification
        QJniObject tagId = tag.callMethod<jbyteArray>("getId");
        if (tagId.isValid()) {
            QJniEnvironment env;
            jsize idLength = env->GetArrayLength(tagId.object<jbyteArray>());
            jbyte* idBytes = env->GetByteArrayElements(tagId.object<jbyteArray>(), nullptr);

            QByteArray uid((const char*)idBytes, idLength);
            env->ReleaseByteArrayElements(tagId.object<jbyteArray>(), idBytes, JNI_ABORT);

            QString uidHex = uid.toHex();
            qDebug() << "KeycardChannelAndroidNfc: Tag UID:" << uidHex;

            emit targetDetected(uidHex);
        }

    } catch (const std::exception& e) {
        qWarning() << "KeycardChannelAndroidNfc: Failed to connect to IsoDep:" << e.what();
        s_connected = false;
    }
}

void KeycardChannelAndroidNfc::handleMultiFrameResponse(QByteArray& response)
{
    if (response.size() < 2) {
        return;
    }

    uint8_t sw1 = (response[response.size() - 2] & 0xFF);
    uint8_t sw2 = (response[response.size() - 1] & 0xFF);

    if (sw1 == 0x61) {
        qWarning() << "🔄 KeycardChannelAndroidNfc: Multi-frame response detected (SW1=0x61)";
        qWarning() << "🔄 Remaining bytes:" << (int)sw2;
        qWarning() << "🔄 Sending GET RESPONSE to retrieve additional data...";

        // Send GET RESPONSE command
        QByteArray getResponseApdu;
        getResponseApdu.append((char)0x00);  // CLA
        getResponseApdu.append((char)0xC0);  // INS (GET RESPONSE)
        getResponseApdu.append((char)0x00);  // P1
        getResponseApdu.append((char)0x00);  // P2
        getResponseApdu.append((char)sw2);   // Le (remaining bytes)

        QJniEnvironment env;

        // Convert to Java byte array
        jbyteArray getResponseArray = env->NewByteArray(getResponseApdu.size());
        env->SetByteArrayRegion(getResponseArray, 0, getResponseApdu.size(),
                               reinterpret_cast<const jbyte*>(getResponseApdu.constData()));

        // Send GET RESPONSE
        QJniObject additionalResponseArray = s_activeIsoDep.callObjectMethod(
            "transceive",
            "([B)[B",
            getResponseArray
        );

        env->DeleteLocalRef(getResponseArray);

        if (additionalResponseArray.isValid()) {
            // Convert additional response
            jbyteArray jAdditionalResponse = additionalResponseArray.object<jbyteArray>();
            jsize additionalLength = env->GetArrayLength(jAdditionalResponse);
            jbyte* additionalBytes = env->GetByteArrayElements(jAdditionalResponse, nullptr);

            QByteArray additionalResponse((const char*)additionalBytes, additionalLength);
            env->ReleaseByteArrayElements(jAdditionalResponse, additionalBytes, JNI_ABORT);

            qDebug() << "🔄 KeycardChannelAndroidNfc: Received additional data:" << additionalResponse.toHex();

            // Combine responses (remove SW from first response, keep SW from final response)
            QByteArray combinedResponse = response.left(response.size() - 2) + additionalResponse;
            qDebug() << "🔄 KeycardChannelAndroidNfc: Combined multi-frame response:" << combinedResponse.toHex();
            response = combinedResponse;
        } else {
            qWarning() << "❌ GET RESPONSE failed!";
        }
    }
}

// REMOVED: Old per-backend enableReaderMode() and disableReaderMode()
// These are no longer needed with the SINGLETON architecture.
// The singleton KeycardNfcReader handles reader mode globally.

// JNI callback implementations
void KeycardChannelAndroidNfc::onJavaTagConnected(JNIEnv* env, jobject thiz, jlong nativePtr, jobject isoDep)
{
    Q_UNUSED(env)
    Q_UNUSED(thiz)

    // CRITICAL: Check if nativePtr is 0 (object destroyed in Java)
    if (nativePtr == 0) {
        qWarning() << "⚠️ nativePtr is 0 in onJavaTagConnected, object was destroyed";
        return;
    }

    // Convert nativePtr back to C++ object
    KeycardChannelAndroidNfc* self = reinterpret_cast<KeycardChannelAndroidNfc*>(nativePtr);
    if (!self) {
        qWarning() << "❌ Invalid nativePtr in onJavaTagConnected!";
        return;
    }

    // CRITICAL: Validate that the object is still the active backend
    // If the object was deleted, s_activeAndroidNfcBackend would be nullptr or different
    if (s_activeAndroidNfcBackend != self) {
        qWarning() << "⚠️ Object mismatch or deleted in onJavaTagConnected, ignoring callback";
        return;
    }

    // Get new tag UID BEFORE updating s_activeIsoDep
    QJniObject newIsoDep(isoDep);
    QJniObject tagId = newIsoDep.callObjectMethod("getTag", "()Landroid/nfc/Tag;");
    QString newUidHex;
    
    if (tagId.isValid()) {
        QJniObject actualTag = tagId;
        QJniObject uidBytes = actualTag.callObjectMethod("getId", "()[B");
        
        if (uidBytes.isValid()) {
            QJniEnvironment jniEnv;
            jbyteArray jUid = uidBytes.object<jbyteArray>();
            jsize uidLength = jniEnv->GetArrayLength(jUid);
            jbyte* uidData = jniEnv->GetByteArrayElements(jUid, nullptr);
            
            QByteArray uid((const char*)uidData, uidLength);
            jniEnv->ReleaseByteArrayElements(jUid, uidData, JNI_ABORT);
            
            newUidHex = uid.toHex();
        }
    }
    
    if (newUidHex.isEmpty()) {
        qWarning() << "⚠️ Failed to get tag UID, ignoring connection";
        return;
    }

    // Store new IsoDep and UID
    s_activeIsoDep = newIsoDep;
    s_connected = true;
    s_currentCardUID = newUidHex;
    
    // Emit targetDetected
    QMetaObject::invokeMethod(self, [self, newUidHex]() {
        emit self->targetDetected(newUidHex);
    }, Qt::QueuedConnection);
}

void KeycardChannelAndroidNfc::onJavaTagDisconnected(JNIEnv* env, jobject thiz, jlong nativePtr)
{
    Q_UNUSED(env)
    Q_UNUSED(thiz)

    // CRITICAL: Check if nativePtr is 0 (object destroyed in Java)
    if (nativePtr == 0) {
        qWarning() << "KeycardChannelAndroidNfc nativePtr is 0 in onJavaTagDisconnected, object was destroyed";
        return;
    }

    // Convert nativePtr back to C++ object
    KeycardChannelAndroidNfc* self = reinterpret_cast<KeycardChannelAndroidNfc*>(nativePtr);
    if (!self) {
        qWarning() << "KeycardChannelAndroidNfc Invalid nativePtr in onJavaTagDisconnected!";
        return;
    }

    // CRITICAL: Validate that the object is still the active backend
    // If the object was deleted, s_activeAndroidNfcBackend would be nullptr or different
    if (s_activeAndroidNfcBackend != self) {
        qWarning() << "⚠️ Object mismatch or deleted in onJavaTagDisconnected, ignoring callback";
        return;
    }

    qDebug() << "KeycardChannelAndroidNfc Android: IsoDep closed (OS timeout or physical removal)";
    qDebug() << "KeycardChannelAndroidNfc Keeping virtual session alive for silent recovery";
    s_activeIsoDep = QJniObject();
    s_connected = false;
}

} // namespace Keycard

