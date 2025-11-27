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
    void setState(ChannelState state) override;
    ChannelState state() const override { return m_state; }

    // NFC intent handling
    static bool checkForNfcIntent(const QJniObject& intent);

    // JNI callback methods (called from Java) - must be public for JNI access
    static void onJavaTagConnected(JNIEnv* env, jobject thiz, jlong nativePtr, jobject isoDep);
    static void onJavaTagDisconnected(JNIEnv* env, jobject thiz, jlong nativePtr);

public slots:
    /**
     * @brief Force immediate re-scan for cards (used after init/factory reset)
     * No-op on Android - card stays in NFC field continuously
     */
    void forceScan();

protected:
    // Override to handle custom events for thread-safe signal emission
    bool event(QEvent* e) override;

private slots:
    void onTagDiscovered(const QJniObject& tag);

private:
    void connectToIsoDep(const QJniObject& tag);
    void handleMultiFrameResponse(QByteArray& response);

    QJniObject m_readerCallback; // Reference to SINGLETON KeycardNfcReader instance

    static QJniObject s_activeIsoDep;
    static bool s_connected;
    static QString s_currentCardUID;  // Track current card for virtual session (iOS-style)
    
    // Channel state (state-driven architecture)
    ChannelState m_state = ChannelState::Idle;
};

} // namespace Keycard

#endif // KEYCARD_CHANNEL_ANDROID_NFC_H
