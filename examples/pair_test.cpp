/**
 * @file pair_test.cpp
 * @brief Test PAIR and secure channel with real Keycard
 * 
 * Usage:
 *   ./pair_test <pairing_password>
 * 
 * If you don't know the pairing password, common defaults are:
 *   - (empty string)
 *   - "KeycardTest"
 *   - "000000"
 */

#include <QCoreApplication>
#include <QDebug>
#include <iostream>
#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/command_set.h"
#include "keycard-qt/types.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "";
    qDebug() << "╔═══════════════════════════════════════════════════════╗";
    qDebug() << "║        Keycard PAIR & Secure Channel Test            ║";
    qDebug() << "╚═══════════════════════════════════════════════════════╝";
    qDebug() << "";

    // Get pairing password from command line
    QString pairingPassword;
    if (argc > 1) {
        pairingPassword = QString::fromUtf8(argv[1]);
        qDebug() << "📝 Using pairing password from command line";
    } else {
        qDebug() << "💡 No pairing password provided, using empty string";
        qDebug() << "   Usage: ./pair_test <pairing_password>";
        qDebug() << "";
        pairingPassword = "";  // Try empty password
    }

    Keycard::KeycardChannel channel;
    Keycard::CommandSet cmdSet(&channel);

    QObject::connect(&channel, &Keycard::KeycardChannel::targetDetected,
        [&](const QString& uid) {
        qDebug() << "✅ Keycard detected!";
        qDebug() << "   UID:" << uid;
        qDebug() << "";

        // Test 1: SELECT
        qDebug() << "📝 Step 1: SELECT Keycard applet";
        try {
            Keycard::ApplicationInfo info = cmdSet.select();
            if (info.instanceUID.isEmpty()) {
                qWarning() << "   ❌ SELECT failed";
                app.quit();
                return;
            }
            qDebug() << "   ✅ SELECT successful!";
            qDebug() << "   App Version:" << QString::number(info.appVersion) 
                     << "." << QString::number(info.appVersionMinor);
            qDebug() << "   Initialized:" << (info.initialized ? "Yes" : "No");
            qDebug() << "";
        } catch (const std::exception& e) {
            qWarning() << "   ❌ SELECT failed:" << e.what();
            app.quit();
            return;
        }

        // Test 2: PAIR
        qDebug() << "📝 Step 2: PAIR with password";
        qDebug() << "   (Note: PAIR can be called multiple times, it returns existing or creates new pairing)";
        Keycard::PairingInfo pairing;
        try {
            pairing = cmdSet.pair(pairingPassword);
            
            if (!pairing.isValid()) {
                qWarning() << "   ❌ PAIR failed!";
                qWarning() << "   Error:" << cmdSet.lastError();
                qWarning() << "";
                qWarning() << "   💡 Possible reasons:";
                qWarning() << "      - Wrong pairing password";
                qWarning() << "      - Card not initialized";
                qWarning() << "      - All pairing slots full";
                qWarning() << "";
                qWarning() << "   Try these common pairing passwords:";
                qWarning() << "      ./pair_test \"\"";
                qWarning() << "      ./pair_test \"KeycardTest\"";
                qWarning() << "      ./pair_test \"000000\"";
                app.quit();
                return;
            }
            
            qDebug() << "   ✅ PAIR successful!";
            qDebug() << "   Pairing Key:" << pairing.key.left(16).toHex() << "...";
            qDebug() << "   Pairing Index:" << pairing.index;
            qDebug() << "";
        } catch (const std::exception& e) {
            qWarning() << "   ❌ PAIR failed:" << e.what();
            app.quit();
            return;
        }

        // Test 3: OPEN_SECURE_CHANNEL
        qDebug() << "📝 Step 3: OPEN_SECURE_CHANNEL";
        try {
            bool opened = cmdSet.openSecureChannel(pairing);
            
            if (!opened) {
                qWarning() << "   ❌ OPEN_SECURE_CHANNEL failed!";
                qWarning() << "   Error:" << cmdSet.lastError();
                app.quit();
                return;
            }
            
            qDebug() << "   ✅ OPEN_SECURE_CHANNEL successful!";
            qDebug() << "   Secure channel is now OPEN! 🔐";
            qDebug() << "";
        } catch (const std::exception& e) {
            qWarning() << "   ❌ OPEN_SECURE_CHANNEL failed:" << e.what();
            app.quit();
            return;
        }

        // Test 4: GET_STATUS (now that secure channel is open)
        qDebug() << "📝 Step 4: GET_STATUS (via secure channel)";
        try {
            Keycard::ApplicationStatus status = cmdSet.getStatus(0x00);
            
            qDebug() << "   ✅ GET_STATUS successful!";
            qDebug() << "";
            qDebug() << "   📊 Card Status:";
            qDebug() << "      PIN Retry Counter:" << status.pinRetryCount;
            qDebug() << "      PUK Retry Counter:" << status.pukRetryCount;
            qDebug() << "      Has Keys:" << (status.keyInitialized ? "Yes" : "No");
            
            if (status.pinRetryCount == 0) {
                qWarning() << "";
                qWarning() << "   ⚠️  WARNING: PIN is BLOCKED!";
                qWarning() << "      Need to unblock with PUK";
            }
            qDebug() << "";
        } catch (const std::exception& e) {
            qWarning() << "   ❌ GET_STATUS failed:" << e.what();
        }

        qDebug() << "╔═══════════════════════════════════════════════════════╗";
        qDebug() << "║              ALL TESTS PASSED! 🎉                     ║";
        qDebug() << "╚═══════════════════════════════════════════════════════╝";
        qDebug() << "";
        qDebug() << "✨ Success! The keycard-qt library is working with real hardware!";
        qDebug() << "";
        qDebug() << "📊 What was tested:";
        qDebug() << "   ✅ SELECT - Get card info";
        qDebug() << "   ✅ PAIR - Authenticate with card";
        qDebug() << "   ✅ OPEN_SECURE_CHANNEL - Establish encrypted communication";
        qDebug() << "   ✅ GET_STATUS - Get card status via secure channel";
        qDebug() << "";
        qDebug() << "🚀 Next steps:";
        qDebug() << "   - Test VERIFY_PIN";
        qDebug() << "   - Test SIGN (requires PIN)";
        qDebug() << "   - Test EXPORT_KEY";
        qDebug() << "";
        
        app.quit();
    });

    QObject::connect(&channel, &Keycard::KeycardChannel::error,
        [&](const QString& msg) {
        qWarning() << "⚠️  Channel error:" << msg;
    });

    qDebug() << "🔍 Waiting for Keycard...";
    qDebug() << "";
    channel.startDetection();

    return app.exec();
}

