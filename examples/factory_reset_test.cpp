/**
 * Factory Reset Test Application
 * 
 * ⚠️  WARNING: THIS WILL PERMANENTLY ERASE ALL DATA ON YOUR KEYCARD! ⚠️
 * 
 * Usage: ./factory_reset_test
 * 
 * This application will:
 * 1. Show current card status
 * 2. Ask for confirmation
 * 3. Perform factory reset (if confirmed)
 * 4. Show card status after reset
 */

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QTextStream>
#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/command_set.h"
#include "keycard-qt/types.h"

class FactoryResetTest : public QObject {
    Q_OBJECT
public:
    explicit FactoryResetTest(Keycard::KeycardChannel* channel, QObject* parent = nullptr)
        : QObject(parent), m_channel(channel), m_cmdSet(new Keycard::CommandSet(channel)) {
        connect(m_channel, &Keycard::KeycardChannel::targetDetected, this, &FactoryResetTest::onCardDetected);
        connect(m_channel, &Keycard::KeycardChannel::targetLost, this, &FactoryResetTest::onCardLost);
        connect(m_channel, &Keycard::KeycardChannel::error, this, &FactoryResetTest::onError);
    }

public slots:
    void start() {
        qDebug() << "";
        qDebug() << "╔════════════════════════════════════════════════════════╗";
        qDebug() << "║                                                        ║";
        qDebug() << "║          ⚠️  FACTORY RESET TEST ⚠️                     ║";
        qDebug() << "║                                                        ║";
        qDebug() << "║  WARNING: THIS WILL ERASE ALL DATA ON YOUR CARD!      ║";
        qDebug() << "║                                                        ║";
        qDebug() << "╚════════════════════════════════════════════════════════╝";
        qDebug() << "";
        qDebug() << "🔍 Waiting for Keycard...";
        qDebug() << "   Please insert your Keycard or tap it on reader";
        qDebug() << "";
        m_channel->startDetection();
    }

private slots:
    void onCardDetected(const QString& uid) {
        qDebug() << "✅ Keycard detected!";
        qDebug() << "   UID:" << uid;
        qDebug() << "";
        
        // Step 1: Get current status
        showCurrentStatus();
        
        // Step 2: Show warnings
        showWarnings();
        
        // Step 3: Wait for user input
        qDebug() << "";
        qDebug() << "⏸️  To continue with factory reset, type 'YES' and press Enter:";
        qDebug() << "   (or type 'NO' to cancel)";
        qDebug() << "";
        
        // Read from stdin
        QTimer::singleShot(0, this, &FactoryResetTest::waitForConfirmation);
    }
    
    void onCardLost() {
        qDebug() << "❌ Keycard removed";
        QCoreApplication::quit();
    }
    
    void onError(const QString& msg) {
        qWarning() << "⚠️  Error:" << msg;
    }
    
    void showCurrentStatus() {
        qDebug() << "📊 Current Card Status:";
        qDebug() << "";
        
        try {
            // SELECT to get card info
            Keycard::ApplicationInfo info = m_cmdSet->select();
            
            if (!info.instanceUID.isEmpty()) {
                qDebug() << "   Instance UID:" << info.instanceUID.toHex();
                qDebug() << "   App Version:" << QString::number(info.appVersion)
                         << "." << QString::number(info.appVersionMinor);
                qDebug() << "   Initialized:" << (info.initialized ? "Yes" : "No");
                qDebug() << "   Available Slots:" << info.availableSlots;
                
                if (!info.keyUID.isEmpty()) {
                    qDebug() << "   Key UID:" << info.keyUID.toHex();
                    qDebug() << "   Has Keys: Yes";
                } else {
                    qDebug() << "   Has Keys: No";
                }
            } else {
                qDebug() << "   ❌ Could not read card info";
                QCoreApplication::quit();
                return;
            }
            
        } catch (const std::exception& e) {
            qWarning() << "   ❌ Error reading card:" << e.what();
            QCoreApplication::quit();
            return;
        }
        
        qDebug() << "";
    }
    
    void showWarnings() {
        qDebug() << "⚠️  ⚠️  ⚠️  CRITICAL WARNING ⚠️  ⚠️  ⚠️";
        qDebug() << "";
        qDebug() << "Factory reset will PERMANENTLY erase:";
        qDebug() << "  ❌ All private keys";
        qDebug() << "  ❌ All pairing data";
        qDebug() << "  ❌ All stored data";
        qDebug() << "  ❌ PIN and PUK";
        qDebug() << "  ❌ Everything on the card";
        qDebug() << "";
        qDebug() << "This operation is IRREVERSIBLE!";
        qDebug() << "";
        qDebug() << "If you have:";
        qDebug() << "  • Funds controlled by this card";
        qDebug() << "  • Important keys stored on it";
        qDebug() << "  • Data you haven't backed up";
        qDebug() << "";
        qDebug() << "STOP NOW and backup your seed phrase first!";
        qDebug() << "";
    }
    
    void waitForConfirmation() {
        QTextStream stream(stdin);
        QString input = stream.readLine().trimmed();
        
        if (input.toUpper() == "YES") {
            qDebug() << "";
            qDebug() << "⚠️  Last chance! Type 'FACTORY RESET' to confirm:";
            QString confirmation = stream.readLine().trimmed();
            
            if (confirmation.toUpper() == "FACTORY RESET") {
                performFactoryReset();
            } else {
                qDebug() << "";
                qDebug() << "❌ Cancelled. Your card is safe.";
                qDebug() << "";
                QCoreApplication::quit();
            }
        } else {
            qDebug() << "";
            qDebug() << "❌ Cancelled. Your card is safe.";
            qDebug() << "";
            QCoreApplication::quit();
        }
    }
    
    void performFactoryReset() {
        qDebug() << "";
        qDebug() << "🔥 Performing factory reset...";
        qDebug() << "";
        
        try {
            bool success = m_cmdSet->factoryReset();
            
            if (success) {
                qDebug() << "✅ Factory reset SUCCESSFUL!";
                qDebug() << "";
                qDebug() << "Your card has been wiped clean.";
                qDebug() << "";
                
                // Wait a moment for card to reset
                QTimer::singleShot(1000, this, &FactoryResetTest::showPostResetStatus);
            } else {
                qWarning() << "❌ Factory reset FAILED!";
                qWarning() << "   Error:" << m_cmdSet->lastError();
                qWarning() << "";
                qWarning() << "Possible reasons:";
                qWarning() << "  - Card already in factory state";
                qWarning() << "  - Communication error";
                qWarning() << "";
                QCoreApplication::quit();
            }
            
        } catch (const std::exception& e) {
            qWarning() << "❌ Factory reset FAILED!";
            qWarning() << "   Exception:" << e.what();
            QCoreApplication::quit();
        }
    }
    
    void showPostResetStatus() {
        qDebug() << "📊 Card Status After Reset:";
        qDebug() << "";
        
        try {
            // SELECT again to see new state
            Keycard::ApplicationInfo info = m_cmdSet->select();
            
            if (!info.instanceUID.isEmpty()) {
                qDebug() << "   Initialized:" << (info.initialized ? "Yes" : "No ✅");
                qDebug() << "   Has Keys:" << (info.keyUID.isEmpty() ? "No ✅" : "Yes");
                qDebug() << "   Available Slots:" << info.availableSlots;
                
                if (!info.initialized) {
                    qDebug() << "";
                    qDebug() << "✅ Card is now in factory state!";
                    qDebug() << "   Ready to be initialized with new data.";
                }
            }
            
        } catch (const std::exception& e) {
            qWarning() << "   ❌ Error reading card:" << e.what();
        }
        
        qDebug() << "";
        qDebug() << "🏁 Factory reset test complete.";
        qDebug() << "";
        QCoreApplication::quit();
    }

private:
    Keycard::KeycardChannel* m_channel;
    QSharedPointer<Keycard::CommandSet> m_cmdSet;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    Keycard::KeycardChannel channel;
    FactoryResetTest test(&channel);
    
    QTimer::singleShot(0, &test, &FactoryResetTest::start);
    
    return app.exec();
}

#include "factory_reset_test.moc"

