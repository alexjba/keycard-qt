// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QByteArray>

namespace Keycard {

/**
 * @brief Abstract interface for Keycard communication backends
 * 
 * This interface defines the contract that all backend implementations
 * (PC/SC, Qt NFC, etc.) must follow. Backends handle platform-specific
 * communication with smart cards/NFC tags.
 * 
 * Backend Selection:
 * - PC/SC: Desktop platforms (Windows, macOS, Linux) via smart card readers
 * - Qt NFC: Mobile platforms (iOS, Android) via NFC
 * 
 * Thread Safety:
 * Implementations should be thread-safe or clearly document threading requirements.
 */
class KeycardChannelBackend : public QObject
{
    Q_OBJECT

public:
    explicit KeycardChannelBackend(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~KeycardChannelBackend() = default;

    /**
     * @brief Start detection/scanning for cards/tags
     * 
     * For PC/SC: Start polling for smart card readers
     * For Qt NFC: Start listening for NFC tag detection
     */
    virtual void startDetection() = 0;

    /**
     * @brief Stop detection/scanning
     */
    virtual void stopDetection() = 0;

    /**
     * @brief Disconnect from the currently connected card/tag
     */
    virtual void disconnect() = 0;

    /**
     * @brief Check if currently connected to a card/tag
     * @return true if connected, false otherwise
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief Transmit APDU command and receive response
     * @param apdu The APDU command to send
     * @return Response APDU from the card
     * @throws std::runtime_error if transmission fails
     */
    virtual QByteArray transmit(const QByteArray& apdu) = 0;

    /**
     * @brief Get backend name for logging/debugging
     * @return Human-readable backend name (e.g., "PC/SC", "Qt NFC")
     */
    virtual QString backendName() const = 0;
    
    /**
     * @brief Set polling interval (if applicable)
     * @param intervalMs Interval in milliseconds
     * 
     * Default implementation does nothing (NFC backends don't poll).
     * PC/SC backend overrides this to control reader polling frequency.
     */
    virtual void setPollingInterval(int intervalMs) { Q_UNUSED(intervalMs); }

signals:
    /**
     * @brief Emitted when reader availability changes (PC/SC only)
     * @param available true if at least one reader is present, false if no readers
     * 
     * This signal allows proper state tracking:
     * - No readers → WaitingForReader state
     * - Reader present → WaitingForCard state  
     * - Card detected → ConnectingCard state
     */
    void readerAvailabilityChanged(bool available);

    /**
     * @brief Emitted when a card/tag is detected and ready for communication
     * @param uid Unique identifier of the detected card/tag (hex string)
     */
    void targetDetected(const QString& uid);

    /**
     * @brief Emitted when a card/tag is removed or connection lost
     */
    void cardRemoved();

    /**
     * @brief Emitted when an error occurs
     * @param message Error description
     */
    void error(const QString& message);

    // Legacy signals for compatibility (can be removed if not used)
    void cardDetected(const QString& uid);
};

} // namespace Keycard

