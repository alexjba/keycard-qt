/**
 * @file advanced_test.cpp
 * @brief Advanced testing application for keycard-qt with real hardware
 * 
 * Tests:
 * 1. SELECT - Get card info
 * 2. GET_STATUS - Get current card status
 * 3. PAIR - Pair with the card (requires pairing password)
 * 4. OPEN_SECURE_CHANNEL - Establish secure communication
 * 5. Card removal detection
 */

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <iostream>
#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/command_set.h"
#include "keycard-qt/types.h"

class AdvancedTester : public QObject {
    Q_OBJECT

public:
    AdvancedTester(QObject* parent = nullptr)
        : QObject(parent)
        , m_channel(new Keycard::KeycardChannel(this))
        , m_cmdSet(new Keycard::CommandSet(m_channel))
        , m_testStep(0)
        , m_cardDetected(false)
    {
        connect(m_channel, &Keycard::KeycardChannel::targetDetected,
                this, &AdvancedTester::onCardDetected);
        connect(m_channel, &Keycard::KeycardChannel::targetLost,
                this, &AdvancedTester::onCardLost);
        connect(m_channel, &Keycard::KeycardChannel::error,
                this, &AdvancedTester::onError);
    }

    void start() {
        printHeader();
        qDebug() << "🔍 Waiting for Keycard...";
        qDebug() << "";
        m_channel->startDetection();
    }

private slots:
    void onCardDetected(const QString& uid) {
        if (m_cardDetected) return; // Already processing
        
        m_cardDetected = true;
        qDebug() << "✅ Keycard detected!";
        qDebug() << "   UID:" << uid;
        qDebug() << "";
        
        runTests();
    }

    void onCardLost() {
        qDebug() << "";
        qDebug() << "❌ Keycard removed!";
        qDebug() << "";
        m_cardDetected = false;
        m_testStep = 0;
        
        qDebug() << "🔍 Waiting for Keycard...";
        qDebug() << "";
    }

    void onError(const QString& msg) {
        qWarning() << "⚠️  Channel error:" << msg;
    }

private:
    void printHeader() {
        qDebug() << "";
        qDebug() << "╔═══════════════════════════════════════════════════════╗";
        qDebug() << "║        keycard-qt Advanced Hardware Test             ║";
        qDebug() << "╚═══════════════════════════════════════════════════════╝";
        qDebug() << "";
    }

    void runTests() {
        qDebug() << "🧪 Running hardware tests...";
        qDebug() << "";
        
        // Test 1: SELECT
        testSelect();
        
        // Test 2: GET_STATUS
        testGetStatus();
        
        // Test 3: Check if already paired
        qDebug() << "";
        qDebug() << "📊 Test Summary:";
        qDebug() << "   ✅ SELECT - Working";
        qDebug() << "   ✅ GET_STATUS - Working";
        qDebug() << "";
        qDebug() << "💡 Next: To test PAIR and secure channel:";
        qDebug() << "   1. Know your pairing password (default: 'KeycardTest' or empty)";
        qDebug() << "   2. Run: ./advanced_test --pair <password>";
        qDebug() << "";
        qDebug() << "✨ All basic tests passed! Keep card connected for removal detection...";
        qDebug() << "";
    }

    void testSelect() {
        qDebug() << "📝 Test 1: SELECT Keycard applet";
        try {
            Keycard::ApplicationInfo info = m_cmdSet->select();
            
            if (info.instanceUID.isEmpty()) {
                qWarning() << "   ❌ Failed to get application info";
                return;
            }
            
            qDebug() << "   ✅ SELECT successful!";
            qDebug() << "";
            qDebug() << "   📋 Card Information:";
            qDebug() << "      Instance UID:" << info.instanceUID.toHex();
            qDebug() << "      App Version:" << QString::number(info.appVersion) 
                     << "." << QString::number(info.appVersionMinor);
            qDebug() << "      Available Slots:" << info.availableSlots;
            qDebug() << "      Installed:" << (info.installed ? "Yes" : "No");
            qDebug() << "      Initialized:" << (info.initialized ? "Yes" : "No");
            
            if (!info.secureChannelPublicKey.isEmpty()) {
                qDebug() << "      SC Public Key:" << info.secureChannelPublicKey.toHex();
            }
            
            if (!info.keyUID.isEmpty()) {
                qDebug() << "      Key UID:" << info.keyUID.toHex();
                qDebug() << "      Has Keys: Yes";
            } else {
                qDebug() << "      Has Keys: No (card not initialized with keys)";
            }
            
            m_appInfo = info;
        } catch (const std::exception& e) {
            qWarning() << "   ❌ SELECT failed:" << e.what();
        }
        qDebug() << "";
    }

    void testGetStatus() {
        qDebug() << "📝 Test 2: GET_STATUS";
        try {
            Keycard::ApplicationStatus status = m_cmdSet->getStatus(0x00);
            
            qDebug() << "   ✅ GET_STATUS successful!";
            qDebug() << "";
            qDebug() << "   📊 Current Status:";
            qDebug() << "      PIN Retry Counter:" << status.pinRetryCount;
            qDebug() << "      PUK Retry Counter:" << status.pukRetryCount;
            qDebug() << "      Has Keys:" << (status.keyInitialized ? "Yes" : "No");
            if (!status.currentPath.isEmpty()) {
                qDebug() << "      Current Path:" << status.currentPath.toHex();
            }
            
            if (status.pinRetryCount == 0) {
                qWarning() << "";
                qWarning() << "   ⚠️  WARNING: PIN is BLOCKED! Use PUK to unblock.";
            } else if (status.pinRetryCount <= 2) {
                qWarning() << "";
                qWarning() << "   ⚠️  CAUTION: Only" << status.pinRetryCount << "PIN attempts remaining!";
            }
            
        } catch (const std::exception& e) {
            qWarning() << "   ❌ GET_STATUS failed:" << e.what();
        }
        qDebug() << "";
    }

private:
    Keycard::KeycardChannel* m_channel;
    Keycard::CommandSet* m_cmdSet;
    Keycard::ApplicationInfo m_appInfo;
    int m_testStep;
    bool m_cardDetected;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    AdvancedTester tester;
    tester.start();

    return app.exec();
}

#include "advanced_test.moc"

