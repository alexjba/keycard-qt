#pragma once

#include "channel_interface.h"
#include <QObject>
#include <QString>
#include <QByteArray>

namespace Keycard {

// Forward declaration of backend interface
class KeycardChannelBackend;

/**
 * @brief Platform-adaptive Keycard communication channel
 * 
 * This class provides a unified interface for Keycard communication
 * across different platforms using a plugin architecture:
 * 
 * - **Desktop (PC/SC)**: Direct smart card reader access via PC/SC
 *   - Windows, macOS, Linux
 *   - Automatic reader detection and polling
 *   - T=0/T=1 protocol support
 * 
 * - **Mobile (Qt NFC)**: NFC tag communication via Qt's NFC API
 *   - iOS (standard Qt NFC)
 *   - Android (Qt NFC with workarounds for Qt 6.9.x bugs)
 * 
 * The appropriate backend is automatically selected at compile time
 * based on the target platform. The backend is implemented via the
 * KeycardChannelBackend interface (see backends/keycard_channel_backend.h).
 * 
 * **Thread Safety**: This class should be used from the main thread.
 * All signals are emitted on the main thread.
 * 
 * **Example Usage**:
 * ```cpp
 * auto channel = new KeycardChannel(this);
 * connect(channel, &KeycardChannel::targetDetected, [](const QString& uid) {
 *     qDebug() << "Keycard detected:" << uid;
 * });
 * channel->startDetection();
 * ```
 */
class KeycardChannel : public QObject, public IChannel {
    Q_OBJECT
    
public:
    explicit KeycardChannel(QObject* parent = nullptr);
    ~KeycardChannel() override;
    
    /**
     * @brief Start detecting cards/tags
     * 
     * Begins scanning for Keycard presence:
     * - PC/SC: Polls smart card readers at regular intervals
     * - Qt NFC: Starts listening for NFC tag detection
     * 
     * Emits targetDetected() when a Keycard is found.
     */
    void startDetection();
    
    /**
     * @brief Stop detecting cards/tags
     * 
     * Stops scanning for Keycards. Does not disconnect from
     * an already connected card.
     */
    void stopDetection();
    
    /**
     * @brief Disconnect from current target
     * 
     * Disconnects from the currently connected Keycard (if any).
     * Emits targetLost() signal.
     */
    void disconnect();
    
    /**
     * @brief Get target UID (card ID)
     * @return UID as hex string, or empty if not connected
     */
    QString targetUid() const;
    
    /**
     * @brief Get backend name for debugging
     * @return Human-readable backend name (e.g., "PC/SC", "Qt NFC")
     */
    QString backendName() const;
    
    // IChannel interface implementation
    /**
     * @brief Transmit APDU command to Keycard
     * @param apdu APDU command bytes
     * @return APDU response bytes
     * @throws std::runtime_error if not connected or transmission fails
     * 
     * This is a blocking call that waits for the card's response.
     * Timeout is backend-specific (typically 5 seconds).
     */
    QByteArray transmit(const QByteArray& apdu) override;
    
    /**
     * @brief Check if connected to a Keycard
     * @return true if connected and ready for communication
     */
    bool isConnected() const override;
    
    /**
     * @brief Set polling interval for card detection (PC/SC only)
     * @param intervalMs Polling interval in milliseconds
     * 
     * Controls how frequently the PC/SC backend checks for card readers.
     * - Lower values (e.g., 50ms): Better responsiveness, more CPU usage
     * - Higher values (e.g., 500ms): Lower CPU usage, slower detection
     * - Default: 100ms
     * 
     * Note: Only affects PC/SC backend. NFC backends don't use polling.
     */
    void setPollingInterval(int intervalMs);
    
signals:
    /**
     * @brief Emitted when a Keycard is detected and ready for communication
     * @param uid Unique identifier of the detected card (hex string)
     * 
     * After this signal, transmit() can be called to communicate with the card.
     */
    void targetDetected(const QString& uid);
    
    /**
     * @brief Emitted when Keycard is removed or connection is lost
     * 
     * After this signal, transmit() will fail until a new card is detected.
     */
    void targetLost();
    
    /**
     * @brief Emitted when an error occurs
     * @param message Human-readable error description
     * 
     * This includes backend initialization errors, communication errors,
     * and platform-specific issues (e.g., NFC not supported).
     */
    void error(const QString& message);
    
private:
    /**
     * @brief Backend instance selected at compile time
     * 
     * Pointer to the platform-specific backend implementation:
     * - KeycardChannelPcsc on desktop
     * - KeycardChannelQtNfc on mobile
     */
    KeycardChannelBackend* m_backend;
    
    QString m_targetUid;  // Cached UID for quick access
};

} // namespace Keycard
