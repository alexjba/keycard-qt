#ifndef KEYCARD_CHANNEL_ANDROID_NFC_H
#define KEYCARD_CHANNEL_ANDROID_NFC_H

#include "keycard_channel_backend.h"
#include <QObject>
#include <QJniObject>
#include <QJniEnvironment>

namespace Keycard {

class KeycardChannelAndroidNfc : public KeycardChannelBackend
{
    Q_OBJECT

public:
    explicit KeycardChannelAndroidNfc(QObject* parent = nullptr);
    ~KeycardChannelAndroidNfc();

    // KeycardChannelBackend interface
    bool isAvailable() const;
    void startDetection() override;
    void stopDetection() override;
    void disconnect() override;
    QByteArray transmit(const QByteArray& apdu) override;
    bool isConnected() const override;
    QString backendName() const override;

    // NFC intent handling
    static bool checkForNfcIntent(const QJniObject& intent);

    // JNI callback methods (called from Java) - must be public for JNI access
    static void onJavaTagConnected(JNIEnv* env, jobject thiz, jlong nativePtr, jobject isoDep);
    static void onJavaTagDisconnected(JNIEnv* env, jobject thiz, jlong nativePtr);

private slots:
    void onTagDiscovered(const QJniObject& tag);

private:
    void setupNfcAdapter();
    void connectToIsoDep(const QJniObject& tag);
    void handleMultiFrameResponse(QByteArray& response);
    void enableReaderMode();
    void disableReaderMode();

    QJniObject m_nfcAdapter;
    QJniObject m_readerCallback; // KeycardNfcReader instance

    static QJniObject s_activeIsoDep;
    static bool s_connected;
};

} // namespace Keycard

#endif // KEYCARD_CHANNEL_ANDROID_NFC_H
