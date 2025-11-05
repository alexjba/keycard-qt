// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#pragma once

#include "keycard_channel_backend.h"
#include <QNearFieldManager>
#include <QNearFieldTarget>
#include <QTimer>
#include <QMap>
#include <QEventLoop>

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
 * - NdefAccess mode for tag detection
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

    QNearFieldManager* m_manager;
    QNearFieldTarget* m_target;
    QTimer* m_pollTimer;
    int m_pollingInterval;
    QByteArray m_targetUid;
    QMap<QNearFieldTarget::RequestId, PendingRequest> m_pendingRequests;
};

} // namespace Keycard

