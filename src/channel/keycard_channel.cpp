// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/backends/keycard_channel_backend.h"
#include <QDebug>

// Platform detection and backend selection
#if (defined(Q_OS_MACOS) || defined(Q_OS_LINUX) || defined(Q_OS_WIN)) && !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    // Desktop platforms use PC/SC
    #define KEYCARD_BACKEND_PCSC
    #include "keycard-qt/backends/keycard_channel_pcsc.h"
#elif defined(Q_OS_IOS)
    // iOS uses Qt NFC
    #define KEYCARD_BACKEND_QT_NFC
    #include "keycard-qt/backends/keycard_channel_qt_nfc.h"
#elif defined(Q_OS_ANDROID)
    // Android: Choose between direct JNI or Qt NFC
    #ifdef USE_ANDROID_NFC_BACKEND
        #define KEYCARD_BACKEND_ANDROID_NFC
        #include "keycard-qt/backends/keycard_channel_android_nfc.h"
    #else
        #define KEYCARD_BACKEND_QT_NFC
        #include "keycard-qt/backends/keycard_channel_qt_nfc.h"
    #endif
#else
    #error "No Keycard backend available for this platform"
#endif

namespace Keycard {

KeycardChannel::KeycardChannel(QObject* parent)
    : QObject(parent)
    , m_backend(nullptr)
{
    qDebug() << "========================================";
    qDebug() << "KeycardChannel: Initializing with plugin architecture";
    
    // Factory: Create appropriate backend based on platform
#ifdef KEYCARD_BACKEND_PCSC
    qDebug() << "KeycardChannel: Creating PC/SC backend (Desktop)";
    m_backend = new KeycardChannelPcsc(this);
#elif defined(KEYCARD_BACKEND_QT_NFC)
    qDebug() << "KeycardChannel: Creating Qt NFC backend (Mobile)";
    m_backend = new KeycardChannelQtNfc(this);
#elif defined(KEYCARD_BACKEND_ANDROID_NFC)
    qDebug() << "KeycardChannel: Creating Android NFC backend (Direct JNI)";
    m_backend = new KeycardChannelAndroidNfc(this);
#else
    #error "No Keycard backend available for this platform"
#endif
    
    if (!m_backend) {
        qCritical() << "KeycardChannel: Failed to create backend!";
        return;
    }
    
    qDebug() << "KeycardChannel: Backend:" << m_backend->backendName();
    qDebug() << "========================================";
    
    // Connect backend signals to our signals (pass-through)
    connect(m_backend, &KeycardChannelBackend::targetDetected,
            this, [this](const QString& uid) {
        m_targetUid = uid;
        emit targetDetected(uid);
    });
    
    connect(m_backend, &KeycardChannelBackend::cardRemoved,
            this, [this]() {
        m_targetUid.clear();
        emit targetLost();
    });
    
    connect(m_backend, &KeycardChannelBackend::error,
            this, &KeycardChannel::error);
}

KeycardChannel::~KeycardChannel()
{
    qDebug() << "KeycardChannel: Destructor";
    // m_backend will be deleted automatically (QObject parent-child)
}

void KeycardChannel::startDetection()
{
    if (m_backend) {
        m_backend->startDetection();
    } else {
        qWarning() << "KeycardChannel: No backend available!";
        emit error("No backend available");
    }
}

void KeycardChannel::stopDetection()
{
    if (m_backend) {
        m_backend->stopDetection();
    } else {
        qWarning() << "KeycardChannel: No backend available!";
    }
}

void KeycardChannel::disconnect()
{
    if (m_backend) {
        m_backend->disconnect();
    } else {
        qWarning() << "KeycardChannel: No backend available!";
    }
}

QString KeycardChannel::targetUid() const
{
    return m_targetUid;
}

QString KeycardChannel::backendName() const
{
    if (m_backend) {
        return m_backend->backendName();
    }
    return "None";
}

QByteArray KeycardChannel::transmit(const QByteArray& apdu)
{
    if (!m_backend) {
        throw std::runtime_error("No backend available");
    }
    return m_backend->transmit(apdu);
}

bool KeycardChannel::isConnected() const
{
    if (m_backend) {
        return m_backend->isConnected();
    }
    return false;
}

void KeycardChannel::setPollingInterval(int intervalMs)
{
    if (m_backend) {
        m_backend->setPollingInterval(intervalMs);
    } else {
        qWarning() << "KeycardChannel: No backend available!";
    }
}

} // namespace Keycard
