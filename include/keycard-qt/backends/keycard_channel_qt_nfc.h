// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#pragma once

#include "keycard_channel_backend.h"
#include <QNearFieldManager>
#include <QNearFieldTarget>
#include <QTimer>
#include <QMap>
#include <QEventLoop>
#include <QDateTime>

namespace Keycard {

/**
 * @brief Qt NFC backend for iOS and Android
 * 
 * Implements communication with Keycard via Qt's NFC API.
 * Used on mobile platforms (iOS, Android).
 * 
 * Platform-Specific Features:
 * 
 * iOS:
 * - Uses standard Qt NFC (works out of the box)
 * - TagTypeSpecificAccess mode for NfcTagType4 (Keycard is ISO 7816-4)
 * 
 * Android:
 * - Qt 6.9.x has bugs requiring workarounds (see qt_nfc_android_workarounds.h)
 * - Workarounds can be disabled via ENABLE_QT_NFC_ANDROID_WORKAROUNDS=OFF
 * - Direct IsoDep communication for APDU transmission
 * 
 * Requirements:
 * - NFC hardware
 * - NFC permissions in app manifest
 */
class KeycardChannelQtNfc : public KeycardChannelBackend
{
    Q_OBJECT

public:
    explicit KeycardChannelQtNfc(QObject* parent = nullptr);
    ~KeycardChannelQtNfc() override;

    // KeycardChannelBackend interface
    void startDetection() override;
    void stopDetection() override;
    void disconnect() override;
    bool isConnected() const override;
    QByteArray transmit(const QByteArray& apdu) override;
    QString backendName() const override { return "Qt NFC"; }
    void setState(ChannelState state) override;
    ChannelState state() const override { return m_state; }
    
    /**
     * @brief Start NFC session explicitly (iOS: shows system drawer)
     * 
     * On iOS, this triggers the CoreNFC UI. Called when app actually needs a card.
     * On Android, this is a no-op (detection already running).
     */
    void startNfcSession();
    
    /**
     * @brief Stop NFC session explicitly (iOS: dismisses system drawer)
     * 
     * On iOS, this dismisses the CoreNFC UI when card is detected or operation cancelled.
     * On Android, this is a no-op.
     */
    void stopNfcSession();
    
    /**
     * @brief Request card at startup (iOS: proactively shows drawer for initial tap)
     * 
     * iOS: Shows NFC drawer and waits for first card tap to initialize app with card metadata.
     * After this, card stays "connected" for subsequent operations (persistent card model).
     * 
     * Android/PC/SC: No-op (card detection already running in background).
     * 
     * @return true if card was detected, false on timeout/error
     */
    bool requestCardAtStartup() override;

private slots:
    /**
     * @brief Handle new NFC target detected by Qt
     * @param target Detected NFC target
     */
    void onTargetDetected(QNearFieldTarget* target);

    /**
     * @brief Handle NFC target lost
     * @param target Lost NFC target
     */
    void onTargetLost(QNearFieldTarget* target);

    /**
     * @brief Handle command sent confirmation
     * @param id Request ID
     * @param command Sent command
     */
    void onCommandSent(QNearFieldTarget::RequestId id, const QByteArray& command);

    /**
     * @brief Handle response received
     * @param id Request ID
     * @param response Received response
     */
    void onResponseReceived(QNearFieldTarget::RequestId id, const QByteArray& response);

    /**
     * @brief Handle target error
     * @param error Error code
     * @param id Request ID
     */
    void onTargetError(QNearFieldTarget::Error error, QNearFieldTarget::RequestId id);

    /**
     * @brief Handle target disconnection
     */
    void onTargetDisconnected();

private:
    // Pending APDU request tracking
    struct PendingRequest {
        QEventLoop* eventLoop = nullptr;
        QByteArray response;
        QNearFieldTarget::Error error = QNearFieldTarget::NoError;
        bool completed = false;
    };
    
    // iOS: Virtual card session for persistent connection model
    // Separates logical connection (card metadata cached) from physical NFC session (drawer shown)
    struct CardSession {
        QByteArray uid;                   // Card UID for verification
        bool logicallyConnected = false;  // TRUE if we have card metadata (can perform operations)
        bool physicallyConnected = false; // TRUE if NFC session is active (drawer shown)
        QDateTime lastSeen;               // For timeout detection
        QNearFieldTarget* physicalTarget = nullptr;  // Current physical target (if session active)
        
        void clear() {
            uid.clear();
            logicallyConnected = false;
            physicallyConnected = false;
            lastSeen = QDateTime();
            physicalTarget = nullptr;
        }
        
        bool isValid() const {
            return logicallyConnected && !uid.isEmpty();
        }
    };
    
    // iOS: STATIC virtual session - persists across KeycardChannelQtNfc object reconstruction
    // This is critical because status-desktop destroys/recreates SessionManager after login,
    // which would otherwise lose the virtual session and cause drawer open/close loops
    static CardSession s_globalCardSession;
    
    /**
     * @brief Wait for card tap with timeout (iOS-specific)
     * @param timeoutMs Timeout in milliseconds
     * @return true if card was tapped, false if timeout
     */
    bool waitForCardTap(int timeoutMs = 10000);

    QNearFieldManager* m_manager;
    QNearFieldTarget* m_target;
    QTimer* m_pollTimer;
    int m_pollingInterval;
    QByteArray m_targetUid;
    QMap<QNearFieldTarget::RequestId, PendingRequest> m_pendingRequests;
    
    // State-driven architecture
    ChannelState m_state = ChannelState::Idle;
    
    bool m_nfcSessionActive = false;  // iOS: Track if NFC session is active
    bool m_detectionEnabled = false;  // Track if detection was initialized
};

} // namespace Keycard

