// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#include "keycard-qt/backends/keycard_channel_pcsc.h"
#include <QDebug>
#include <stdexcept>

#ifdef Q_OS_WIN
#include <winscard.h>
#elif defined(Q_OS_LINUX)
// Linux PCSC headers define all types
#include <PCSC/winscard.h>
#include <PCSC/pcsclite.h>
#else
// macOS PCSC headers need manual type definitions
#include <PCSC/winscard.h>
#include <PCSC/pcsclite.h>

// macOS doesn't define these Windows-style types
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint8_t BYTE;
typedef char* LPSTR;
typedef const uint8_t* LPCBYTE;
#endif

namespace Keycard {

// Pimpl structure to hide PC/SC types from MOC
struct PcscState {
    SCARDCONTEXT context = 0;
    SCARDHANDLE cardHandle = 0;
    DWORD activeProtocol = 0;
    bool contextEstablished = false;
};

KeycardChannelPcsc::KeycardChannelPcsc(QObject* parent)
    : KeycardChannelBackend(parent)
    , m_pcscState(new PcscState())
    , m_connected(false)
    , m_pollTimer(new QTimer(this))
    , m_pollingInterval(100)
{
    qDebug() << "KeycardChannelPcsc: Initialized (Desktop smart card reader)";
    connect(m_pollTimer, &QTimer::timeout, this, &KeycardChannelPcsc::checkForCards);
}

KeycardChannelPcsc::~KeycardChannelPcsc()
{
    stopDetection();
    disconnectFromCard();
    releaseContext();
    delete m_pcscState;
}

void KeycardChannelPcsc::establishContext()
{
    if (m_pcscState->contextEstablished) {
        return;
    }
    
    LONG rv = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &m_pcscState->context);
    
    if (rv != SCARD_S_SUCCESS) {
        QString msg = QString("Failed to establish PC/SC context: 0x%1").arg(rv, 0, 16);
        qWarning() << "KeycardChannelPcsc:" << msg;
        emit error(msg);
        return;
    }
    
    m_pcscState->contextEstablished = true;
    qDebug() << "KeycardChannelPcsc: PC/SC context established";
}

void KeycardChannelPcsc::releaseContext()
{
    if (m_pcscState->contextEstablished && m_pcscState->context) {
        SCardReleaseContext(m_pcscState->context);
        m_pcscState->context = 0;
        m_pcscState->contextEstablished = false;
        qDebug() << "KeycardChannelPcsc: PC/SC context released";
    }
}

QStringList KeycardChannelPcsc::listReaders()
{
    QStringList readers;
    
    if (!m_pcscState->contextEstablished) {
        return readers;
    }
    
#ifdef Q_OS_WIN
    LPWSTR mszReaders = NULL;
    DWORD dwReaders = SCARD_AUTOALLOCATE;
    
    LONG rv = SCardListReadersW(m_pcscState->context, NULL, (LPWSTR)&mszReaders, &dwReaders);
    
    if (rv == SCARD_S_SUCCESS && mszReaders) {
        LPWSTR reader = mszReaders;
        while (*reader) {
            readers.append(QString::fromWCharArray(reader));
            reader += wcslen(reader) + 1;
        }
        SCardFreeMemory(m_pcscState->context, mszReaders);
    }
#else
    // macOS/Linux: Get buffer size first, then allocate
    DWORD dwReaders = 0;
    LONG rv = SCardListReaders(m_pcscState->context, NULL, NULL, &dwReaders);
    
    if (rv == SCARD_S_SUCCESS && dwReaders > 0) {
        LPSTR mszReaders = new char[dwReaders];
        
        rv = SCardListReaders(m_pcscState->context, NULL, mszReaders, &dwReaders);
        
        if (rv == SCARD_S_SUCCESS) {
            char* reader = mszReaders;
            while (*reader) {
                readers.append(QString::fromUtf8(reader));
                reader += strlen(reader) + 1;
            }
        }
        
        delete[] mszReaders;
    }
#endif
    
    return readers;
}

bool KeycardChannelPcsc::connectToReader(const QString& readerName)
{
    if (m_connected) {
        return true;  // Already connected
    }
    
    if (!m_pcscState->contextEstablished) {
        establishContext();
        if (!m_pcscState->contextEstablished) {
            return false;
        }
    }
    
    qDebug() << "KeycardChannelPcsc: Connecting to card in reader:" << readerName;
    
#ifdef Q_OS_WIN
    LONG rv = SCardConnectW(
        m_pcscState->context,
        (LPCWSTR)readerName.utf16(),
        SCARD_SHARE_SHARED,
        SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
        &m_pcscState->cardHandle,
        &m_pcscState->activeProtocol
    );
#else
    QByteArray readerBytes = readerName.toUtf8();
    LONG rv = SCardConnect(
        m_pcscState->context,
        readerBytes.constData(),
        SCARD_SHARE_SHARED,
        SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
        &m_pcscState->cardHandle,
        &m_pcscState->activeProtocol
    );
#endif
    
    if (rv != SCARD_S_SUCCESS) {
        qDebug() << "KeycardChannelPcsc: Failed to connect to card:" << QString("0x%1").arg(rv, 0, 16);
        return false;
    }
    
    m_lastDetectedReader = readerName;
    m_connected = true;
    
    // Get ATR
    m_lastATR = getATR();
    
    qDebug() << "KeycardChannelPcsc: Connected to card";
    qDebug() << "KeycardChannelPcsc: Protocol:" << (m_pcscState->activeProtocol == SCARD_PROTOCOL_T0 ? "T=0" : "T=1");
    qDebug() << "KeycardChannelPcsc: ATR:" << m_lastATR.toHex();
    
    return true;
}

void KeycardChannelPcsc::disconnectFromCard()
{
    if (m_pcscState->cardHandle) {
        SCardDisconnect(m_pcscState->cardHandle, SCARD_LEAVE_CARD);
        m_pcscState->cardHandle = 0;
    }
    
    if (m_connected) {
        qDebug() << "KeycardChannelPcsc: Disconnected from card";
        m_connected = false;
        m_lastATR.clear();
        m_lastDetectedReader.clear();
    }
}

QByteArray KeycardChannelPcsc::getATR()
{
    if (!m_connected) {
        return QByteArray();
    }
    
    // Get ATR (Answer To Reset) which contains card info
    BYTE pbAtr[33];
    DWORD dwAtrLen = sizeof(pbAtr);
    DWORD dwState, dwProtocol;
    
#ifdef Q_OS_WIN
    WCHAR szReader[200];
    DWORD dwReaderLen = sizeof(szReader) / sizeof(WCHAR);
    
    LONG rv = SCardStatusW(
        m_pcscState->cardHandle,
        szReader,
        &dwReaderLen,
        &dwState,
        &dwProtocol,
        pbAtr,
        &dwAtrLen
    );
#else
    char szReader[200];
    DWORD dwReaderLen = sizeof(szReader);
    
    LONG rv = SCardStatus(
        m_pcscState->cardHandle,
        szReader,
        &dwReaderLen,
        &dwState,
        &dwProtocol,
        pbAtr,
        &dwAtrLen
    );
#endif
    
    if (rv == SCARD_S_SUCCESS) {
        return QByteArray((char*)pbAtr, dwAtrLen);
    }
    
    return QByteArray();
}

void KeycardChannelPcsc::startDetection()
{
    qDebug() << "KeycardChannelPcsc: Starting card detection";
    
    establishContext();
    
    if (!m_pcscState->contextEstablished) {
        emit error("Failed to establish PC/SC context");
        return;
    }
    
    // Start polling for cards
    m_pollTimer->start(m_pollingInterval);
    
    // Do immediate check
    checkForCards();
}

void KeycardChannelPcsc::stopDetection()
{
    qDebug() << "KeycardChannelPcsc: Stopping card detection";
    m_pollTimer->stop();
}

void KeycardChannelPcsc::checkForCards()
{
    QStringList readers = listReaders();
    
    if (readers.isEmpty()) {
        if (m_connected) {
            disconnectFromCard();
            emit cardRemoved();
        }
        return;
    }
    
    // Try to connect to first reader with a card
    for (const QString& reader : readers) {
        if (connectToReader(reader)) {
            // Use ATR as UID (last 4 bytes for uniqueness)
            QString uid = m_lastATR.right(4).toHex();
            emit targetDetected(uid);
            emit cardDetected(uid);  // Legacy signal
            return;
        }
    }
    
    // No cards found
    if (m_connected) {
        disconnectFromCard();
        emit cardRemoved();
    }
}

void KeycardChannelPcsc::disconnect()
{
    disconnectFromCard();
}

QByteArray KeycardChannelPcsc::transmit(const QByteArray& apdu)
{
    if (!m_connected || !m_pcscState->cardHandle) {
        throw std::runtime_error("Not connected to any card");
    }
    
    qDebug() << "KeycardChannelPcsc: Transmitting APDU:" << apdu.toHex();
    
    // Prepare send/receive structures
    SCARD_IO_REQUEST pioSendPci;
    if (m_pcscState->activeProtocol == SCARD_PROTOCOL_T0) {
        pioSendPci = *SCARD_PCI_T0;
    } else {
        pioSendPci = *SCARD_PCI_T1;
    }
    
    BYTE pbRecvBuffer[258];  // Max APDU response
    DWORD dwRecvLength = sizeof(pbRecvBuffer);
    
    LONG rv = SCardTransmit(
        m_pcscState->cardHandle,
        &pioSendPci,
        (LPCBYTE)apdu.constData(),
        apdu.size(),
        NULL,
        pbRecvBuffer,
        &dwRecvLength
    );
    
    if (rv != SCARD_S_SUCCESS) {
        QString msg = QString("SCardTransmit failed: 0x%1").arg(rv, 0, 16);
        qWarning() << "KeycardChannelPcsc:" << msg;
        throw std::runtime_error(msg.toStdString());
    }
    
    QByteArray response((char*)pbRecvBuffer, dwRecvLength);
    qDebug() << "KeycardChannelPcsc: Received response:" << response.toHex();
    
    return response;
}

bool KeycardChannelPcsc::isConnected() const
{
    return m_connected;
}

void KeycardChannelPcsc::setPollingInterval(int intervalMs)
{
    if (intervalMs < 10) {
        qWarning() << "KeycardChannelPcsc: Polling interval too small, using 10ms minimum";
        intervalMs = 10;
    }
    if (intervalMs > 10000) {
        qWarning() << "KeycardChannelPcsc: Polling interval too large, using 10s maximum";
        intervalMs = 10000;
    }
    
    m_pollingInterval = intervalMs;
    qDebug() << "KeycardChannelPcsc: Polling interval set to" << m_pollingInterval << "ms";
    
    // If already polling, restart with new interval
    if (m_pollTimer->isActive()) {
        m_pollTimer->setInterval(m_pollingInterval);
    }
}

} // namespace Keycard

