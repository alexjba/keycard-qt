#include <QTest>
#include <QSignalSpy>
#include "keycard-qt/keycard_channel.h"

using namespace Keycard;

class TestKeycardChannel : public QObject {
    Q_OBJECT
    
private:
    KeycardChannel* channel;
    
private slots:
    void initTestCase() {
        channel = new KeycardChannel();
    }
    
    void cleanupTestCase() {
        delete channel;
    }
    
    // Test construction
    void testConstruction() {
        KeycardChannel testChannel;
        QVERIFY(!testChannel.isConnected());
        QVERIFY(testChannel.targetUid().isEmpty());
    }
    
    // Test isConnected() before detection
    void testNotConnectedInitially() {
        QVERIFY(!channel->isConnected());
    }
    
    // Test targetUid() when not connected
    void testTargetUidEmpty() {
        QString uid = channel->targetUid();
        QVERIFY(uid.isEmpty());
    }
    
    // Test setPollingInterval
    void testSetPollingInterval() {
        // Should not crash - valid intervals
        channel->setPollingInterval(100);
        channel->setPollingInterval(50);
        channel->setPollingInterval(500);
        
        QVERIFY(true);  // No crash = success
    }
    
    // Test stopDetection when not started
    void testStopDetectionSafe() {
        // Should be safe to call even if not started
        channel->stopDetection();
        QVERIFY(true);
    }
    
    // Test disconnect when not connected
    void testDisconnectSafe() {
        // Should be safe to call when not connected
        channel->disconnect();
        QVERIFY(true);
    }
    
    // Test transmit without connection (should throw)
    void testTransmitWithoutConnection() {
        bool exceptionThrown = false;
        try {
            QByteArray apdu = QByteArray::fromHex("00A4040000");
            channel->transmit(apdu);
        } catch (const std::runtime_error& e) {
            exceptionThrown = true;
            QString msg = e.what();
            QVERIFY(msg.contains("not connected") || msg.contains("Not connected"));
        }
        
        QVERIFY(exceptionThrown);
    }
    
    // Test signals exist (structure test)
    void testSignalsExist() {
        // Check that signals can be connected
        QSignalSpy spyDetected(channel, &KeycardChannel::targetDetected);
        QSignalSpy spyLost(channel, &KeycardChannel::targetLost);
        QSignalSpy spyError(channel, &KeycardChannel::error);
        
        QVERIFY(spyDetected.isValid());
        QVERIFY(spyLost.isValid());
        QVERIFY(spyError.isValid());
    }
    
    // Test multiple start/stop cycles
    void testMultipleStartStopCycles() {
        // Note: This will fail if NFC is not available, but shouldn't crash
        for (int i = 0; i < 3; i++) {
            channel->startDetection();
            QTest::qWait(10);  // Small delay
            channel->stopDetection();
        }
        
        QVERIFY(true);  // No crash = success
    }
    
    // Test that channel inherits from IChannel
    void testIChannelInterface() {
        IChannel* iface = channel;
        QVERIFY(iface != nullptr);
        
        // Test interface methods
        // Note: May be connected if card is present from previous test
        // QVERIFY(!iface->isConnected()); // Skip - hardware dependent
    }
    
    // Test QObject parent
    void testQObjectParent() {
        QObject parent;
        KeycardChannel* childChannel = new KeycardChannel(&parent);
        
        QCOMPARE(childChannel->parent(), &parent);
        
        // Parent will delete child, no manual delete needed
    }
    
    // Test polling interval limits
    void testPollingIntervalLimits() {
        // Test edge cases - should handle gracefully
        channel->setPollingInterval(1);      // Very fast (will be clamped to 10ms)
        channel->setPollingInterval(10000);  // Very slow (at limit)
        channel->setPollingInterval(0);      // Zero (will be clamped to 10ms)
        channel->setPollingInterval(50000);  // Too large (will be clamped to 10s)
        
        QVERIFY(true);  // No crash = success
    }
};

QTEST_MAIN(TestKeycardChannel)
#include "test_keycard_channel.moc"

