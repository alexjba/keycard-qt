#include "keycard-qt/backends/keycard_channel_android_nfc.h"
#include <QCoreApplication>
#include <QDebug>
#include <QtCore/private/qandroidextras_p.h>

namespace Keycard {

// Static members
QJniObject KeycardChannelAndroidNfc::s_activeIsoDep;
bool KeycardChannelAndroidNfc::s_connected = false;

// Global callback for NFC intents
static KeycardChannelAndroidNfc* s_activeAndroidNfcBackend = nullptr;

// Forward declaration for JNI registration
static void registerJniMethods();

KeycardChannelAndroidNfc::KeycardChannelAndroidNfc(QObject* parent)
    : KeycardChannelBackend(parent)
{
    qDebug() << "KeycardChannelAndroidNfc: Constructor";

    // Register this instance as the active backend
    s_activeAndroidNfcBackend = this;

    // Register JNI native methods for KeycardNfcReader callbacks
    registerJniMethods();

    setupNfcAdapter();
}

static void registerJniMethods()
{
    qDebug() << "KeycardChannelAndroidNfc: Registering JNI native methods";

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
        qWarning() << "❌ Could not find KeycardNfcReader class!";
        return;
    }

    if (env->RegisterNatives(clazz, methods, 2) < 0) {
        qWarning() << "❌ Failed to register JNI native methods!";
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        env->DeleteLocalRef(clazz);
        return;
    }

    env->DeleteLocalRef(clazz);
    qDebug() << "✅ JNI native methods registered successfully!";
}

KeycardChannelAndroidNfc::~KeycardChannelAndroidNfc()
{
    qDebug() << "KeycardChannelAndroidNfc: Destructor";

    // Unregister this instance if it's the active one
    if (s_activeAndroidNfcBackend == this) {
        s_activeAndroidNfcBackend = nullptr;
    }

    stopDetection();

    if (s_connected && s_activeIsoDep.isValid()) {
        s_activeIsoDep.callMethod<void>("close");
        s_activeIsoDep = QJniObject();
        s_connected = false;
    }
}

bool KeycardChannelAndroidNfc::isAvailable() const
{
    if (!m_nfcAdapter.isValid()) {
        return false;
    }

    jboolean nfcEnabled = m_nfcAdapter.callMethod<jboolean>("isEnabled");
    return nfcEnabled;
}

void KeycardChannelAndroidNfc::startDetection()
{
    qDebug() << "KeycardChannelAndroidNfc: Starting NFC detection with enableReaderMode()";

    if (!isAvailable()) {
        qWarning() << "KeycardChannelAndroidNfc: NFC not available";
        return;
    }

    enableReaderMode();
}

void KeycardChannelAndroidNfc::stopDetection()
{
    qDebug() << "KeycardChannelAndroidNfc: Stopping NFC detection";

    disableReaderMode();

    qDebug() << "KeycardChannelAndroidNfc: NFC detection stopped";
}

void KeycardChannelAndroidNfc::disconnect()
{
    qDebug() << "KeycardChannelAndroidNfc: Disconnecting from card";

    if (s_connected && s_activeIsoDep.isValid()) {
        try {
            s_activeIsoDep.callMethod<void>("close");
        } catch (const std::exception& e) {
            qWarning() << "KeycardChannelAndroidNfc: Error closing connection:" << e.what();
        }
        s_activeIsoDep = QJniObject();
        s_connected = false;
        emit cardRemoved();
    }
}

QString KeycardChannelAndroidNfc::backendName() const
{
    return "Android NFC";
}

QByteArray KeycardChannelAndroidNfc::transmit(const QByteArray& apdu)
{
    if (!s_connected || !s_activeIsoDep.isValid()) {
        qWarning() << "KeycardChannelAndroidNfc: Not connected to IsoDep";
        return QByteArray();
    }

    qDebug() << "KeycardChannelAndroidNfc: Transmitting APDU (" << apdu.size() << " bytes):" << apdu.toHex();

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
            qWarning() << "KeycardChannelAndroidNfc: Transceive failed";
            s_activeIsoDep.callMethod<void>("close");
            s_activeIsoDep = QJniObject();
            s_connected = false;
            emit cardRemoved();
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

    if (s_connected) {
        qDebug() << "KeycardChannelAndroidNfc: Already connected, ignoring new tag";
        return;
    }

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

void KeycardChannelAndroidNfc::setupNfcAdapter()
{
    qDebug() << "KeycardChannelAndroidNfc: Setting up NFC adapter";

    // Get the Android context
    QJniObject context = QJniObject::callStaticObjectMethod("org/qtproject/qt/android/QtNative",
                                                           "getContext",
                                                           "()Landroid/content/Context;");

    if (!context.isValid()) {
        qWarning() << "KeycardChannelAndroidNfc: Could not get Android context";
        return;
    }

    // Get default NFC adapter using NfcAdapter.getDefaultAdapter(Context)
    // This is the React Native approach!
    m_nfcAdapter = QJniObject::callStaticObjectMethod(
        "android/nfc/NfcAdapter",
        "getDefaultAdapter",
        "(Landroid/content/Context;)Landroid/nfc/NfcAdapter;",
        context.object<jobject>()
    );

    if (!m_nfcAdapter.isValid()) {
        qWarning() << "KeycardChannelAndroidNfc: NFC not available on this device";
        return;
    }

    qDebug() << "✅ KeycardChannelAndroidNfc: NFC adapter initialized (using NfcAdapter.getDefaultAdapter)";
}

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
        // Connect to the tag
        isoDep.callMethod<void>("connect");
        qDebug() << "KeycardChannelAndroidNfc: Connected to IsoDep";

        // Set timeout to 120 seconds (matching React Native)
        isoDep.callMethod<void>("setTimeout", "(I)V", 120000);
        qDebug() << "KeycardChannelAndroidNfc: Timeout set to 120 seconds";

        // Check capabilities
        bool supportsExtended = isoDep.callMethod<jboolean>("isExtendedLengthApduSupported");
        qDebug() << "KeycardChannelAndroidNfc: Extended APDU supported:" << supportsExtended;

        int maxLength = isoDep.callMethod<jint>("getMaxTransceiveLength");
        qDebug() << "KeycardChannelAndroidNfc: Max transceive length:" << maxLength << "bytes";

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
            emit cardDetected(uidHex);
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

void KeycardChannelAndroidNfc::enableReaderMode()
{
    qDebug() << "KeycardChannelAndroidNfc: Enabling reader mode (React Native style)";

    // Get the Android activity
    QJniObject activity = QJniObject::callStaticObjectMethod("org/qtproject/qt/android/QtNative",
                                                             "activity",
                                                             "()Landroid/app/Activity;");

    if (!activity.isValid()) {
        qWarning() << "KeycardChannelAndroidNfc: Could not get Android activity";
        return;
    }

    // Create KeycardNfcReader instance (our Java callback class)
    // Pass 'this' pointer to Java so callbacks can find us
    jlong nativePtr = reinterpret_cast<jlong>(this);
    m_readerCallback = QJniObject("im/status/keycard/android/KeycardNfcReader",
                                 "(J)V",
                                 nativePtr);

    if (!m_readerCallback.isValid()) {
        qWarning() << "KeycardChannelAndroidNfc: Could not create KeycardNfcReader";
        return;
    }

    qDebug() << "✅ KeycardNfcReader created with nativePtr:" << (void*)this;

    // Get NFC flags constants
    // FLAG_READER_NFC_A = 0x1
    // FLAG_READER_SKIP_NDEF_CHECK = 0x80
    jint flags = 0x1 | 0x80;  // NFC-A + SKIP_NDEF_CHECK (matching React Native)

    // Enable reader mode with our callback
    // void enableReaderMode(Activity activity, ReaderCallback callback, int flags, Bundle extras)
    m_nfcAdapter.callMethod<void>("enableReaderMode",
                                 "(Landroid/app/Activity;Landroid/nfc/NfcAdapter$ReaderCallback;ILandroid/os/Bundle;)V",
                                 activity.object<jobject>(),
                                 m_readerCallback.object<jobject>(),
                                 flags,
                                 nullptr);  // No extras

    qDebug() << "✅ NfcAdapter.enableReaderMode() called with flags:" << QString("0x%1").arg(flags, 0, 16);
    qDebug() << "✅ KeycardChannelAndroidNfc: Reader mode enabled (React Native style)!";
}

void KeycardChannelAndroidNfc::disableReaderMode()
{
    qDebug() << "KeycardChannelAndroidNfc: Disabling reader mode";

    if (!m_nfcAdapter.isValid()) {
        return;
    }

    // Get the Android activity
    QJniObject activity = QJniObject::callStaticObjectMethod("org/qtproject/qt/android/QtNative",
                                                             "activity",
                                                             "()Landroid/app/Activity;");

    if (activity.isValid()) {
        m_nfcAdapter.callMethod<void>("disableReaderMode",
                                     "(Landroid/app/Activity;)V",
                                     activity.object<jobject>());
        qDebug() << "✅ NfcAdapter.disableReaderMode() called";
    }

    m_readerCallback = QJniObject();
}

// JNI callback implementations
void KeycardChannelAndroidNfc::onJavaTagConnected(JNIEnv* env, jobject thiz, jlong nativePtr, jobject isoDep)
{
    Q_UNUSED(env)
    Q_UNUSED(thiz)

    qDebug() << "🎯 KeycardChannelAndroidNfc: onJavaTagConnected called! nativePtr:" << (void*)nativePtr;

    // Convert nativePtr back to C++ object
    KeycardChannelAndroidNfc* self = reinterpret_cast<KeycardChannelAndroidNfc*>(nativePtr);
    if (!self) {
        qWarning() << "❌ Invalid nativePtr in onJavaTagConnected!";
        return;
    }

    // Store the IsoDep object globally
    s_activeIsoDep = QJniObject(isoDep);
    s_connected = true;

    qDebug() << "✅ IsoDep connection stored, calling connectToIsoDep...";

    // Create a temporary tag object (we only need IsoDep, so create a minimal Tag)
    // Actually, we don't need the Tag at all - we already have IsoDep!
    // Just emit the signals directly

    // Get tag UID
    QJniObject tagId = s_activeIsoDep.callObjectMethod("getTag", "()Landroid/nfc/Tag;");
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
            
            QString uidHex = uid.toHex();
            qDebug() << "✅ Tag UID:" << uidHex;
            
            // Emit signals
            emit self->targetDetected(uidHex);
            emit self->cardDetected(uidHex);
        }
    }
}

void KeycardChannelAndroidNfc::onJavaTagDisconnected(JNIEnv* env, jobject thiz, jlong nativePtr)
{
    Q_UNUSED(env)
    Q_UNUSED(thiz)

    qDebug() << "🎯 KeycardChannelAndroidNfc: onJavaTagDisconnected called! nativePtr:" << (void*)nativePtr;

    // Convert nativePtr back to C++ object
    KeycardChannelAndroidNfc* self = reinterpret_cast<KeycardChannelAndroidNfc*>(nativePtr);
    if (!self) {
        qWarning() << "❌ Invalid nativePtr in onJavaTagDisconnected!";
        return;
    }

    s_activeIsoDep = QJniObject();
    s_connected = false;

    qDebug() << "✅ IsoDep disconnected, emitting cardRemoved signal";
    emit self->cardRemoved();
}

} // namespace Keycard
