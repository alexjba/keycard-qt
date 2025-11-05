// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#pragma once

#include "keycard_channel_backend.h"
#include <QTimer>
#include <QString>
#include <QStringList>

namespace Keycard {

// Forward declarations to hide PC/SC types from MOC
struct PcscState;

/**
 * @brief PC/SC backend for desktop smart card readers
 * 
 * Implements communication with Keycard via PC/SC (Personal Computer/Smart Card)
 * standard, used on desktop platforms (Windows, macOS, Linux).
 * 
 * Features:
 * - Automatic reader detection and polling
 * - T=1 protocol support
 * - APDU transmission with proper error handling
 * 
 * Requirements:
 * - PC/SC daemon running (pcscd on Linux/macOS, built-in on Windows)
 * - Smart card reader hardware
 */
class KeycardChannelPcsc : public KeycardChannelBackend
{
    Q_OBJECT

public:
    explicit KeycardChannelPcsc(QObject* parent = nullptr);
    ~KeycardChannelPcsc() override;

    // KeycardChannelBackend interface
    void startDetection() override;
    void stopDetection() override;
    void disconnect() override;
    bool isConnected() const override;
    QByteArray transmit(const QByteArray& apdu) override;
    QString backendName() const override { return "PC/SC"; }
    void setPollingInterval(int intervalMs) override;

private slots:
    /**
     * @brief Poll for cards in available readers (called by timer)
     */
    void checkForCards();

private:
    /**
     * @brief Establish PC/SC context for communication
     */
    void establishContext();

    /**
     * @brief Release PC/SC context
     */
    void releaseContext();

    /**
     * @brief List all available smart card readers
     * @return List of reader names
     */
    QStringList listReaders();

    /**
     * @brief Connect to a specific reader
     * @param readerName Name of the reader to connect to
     * @return true if connection successful
     */
    bool connectToReader(const QString& readerName);

    /**
     * @brief Disconnect from the current reader
     */
    void disconnectFromCard();

    /**
     * @brief Get ATR (Answer To Reset) from connected card
     * @return ATR bytes
     */
    QByteArray getATR();

    // PC/SC state (hidden via pimpl to avoid MOC issues with PC/SC types)
    PcscState* m_pcscState;
    bool m_connected;

    // Card detection
    QTimer* m_pollTimer;
    int m_pollingInterval;
    QString m_lastDetectedReader;
    QByteArray m_lastATR;
};

} // namespace Keycard

