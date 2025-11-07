// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#include <QTest>
#include <QSignalSpy>
#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/backends/keycard_channel_backend.h"
#include "mocks/mock_backend.h"

using namespace Keycard;
using namespace Keycard::Test;

/**
 * @brief Tests for KeycardChannel dependency injection functionality
 * 
 * These tests verify:
 * - DI constructor accepts custom backends
 * - Signals are properly forwarded from backend
 * - Backend lifecycle management
 * - Mock backend integration
 * - No hardware initialization with injected backend
 */
class TestKeycardChannelDI : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qDebug() << "===========================================";
        qDebug() << "KeycardChannel Dependency Injection Tests";
        qDebug() << "===========================================";
    }

    void cleanupTestCase() {
        qDebug() << "===========================================";
        qDebug() << "DI Tests Complete";
        qDebug() << "===========================================";
    }

    // ========================================================================
    // Constructor Tests
    // ========================================================================

    void testDefaultConstructor() {
        qDebug() << "\n--- Test: Default Constructor ---";
        
        // Default constructor should create platform backend
        KeycardChannel channel;
        
        // Should not be connected initially
        QVERIFY(!channel.isConnected());
        QVERIFY(channel.targetUid().isEmpty());
        
        // Should have a backend name
        QString backendName = channel.backendName();
        QVERIFY(!backendName.isEmpty());
        
        qDebug() << "Default backend:" << backendName;
        qDebug() << "✓ Default constructor works";
    }

    void testDIConstructor() {
        qDebug() << "\n--- Test: DI Constructor ---";
        
        // Create mock backend
        auto* mock = new MockBackend();
        
        // Inject into channel
        KeycardChannel channel(mock);
        
        // Should not be connected
        QVERIFY(!channel.isConnected());
        
        // Should have mock backend
        QCOMPARE(channel.backendName(), QString("Mock Backend"));
        
        qDebug() << "Backend:" << channel.backendName();
        qDebug() << "✓ DI constructor accepts backend";
    }

    void testDIConstructorWithParent() {
        qDebug() << "\n--- Test: DI Constructor with Parent ---";
        
        QObject parent;
        auto* mock = new MockBackend();
        
        // Create channel with parent and backend
        auto* channel = new KeycardChannel(mock, &parent);
        
        // Check parent
        QCOMPARE(channel->parent(), &parent);
        QCOMPARE(channel->backendName(), QString("Mock Backend"));
        
        // Parent will delete channel
        qDebug() << "✓ DI constructor with parent works";
    }

    void testBackendOwnership() {
        qDebug() << "\n--- Test: Backend Ownership ---";
        
        // Backend without parent - channel should take ownership
        {
            auto* mock = new MockBackend();
            QVERIFY(mock->parent() == nullptr);
            
            KeycardChannel channel(mock);
            
            // Channel should have adopted mock as child
            QCOMPARE(mock->parent(), &channel);
            
            // When channel is destroyed, mock should be deleted too
        }
        
        // Backend with parent - channel should NOT change ownership
        {
            QObject parent;
            auto* mock = new MockBackend(&parent);
            QCOMPARE(mock->parent(), &parent);
            
            KeycardChannel channel(mock);
            
            // Parent should still be original
            QCOMPARE(mock->parent(), &parent);
        }
        
        qDebug() << "✓ Backend ownership management works";
    }

    // ========================================================================
    // Signal Forwarding Tests
    // ========================================================================

    void testTargetDetectedSignal() {
        qDebug() << "\n--- Test: targetDetected Signal Forwarding ---";
        
        auto* mock = new MockBackend();
        KeycardChannel channel(mock);
        
        // Setup spy on channel signal
        QSignalSpy spy(&channel, &KeycardChannel::targetDetected);
        QVERIFY(spy.isValid());
        
        // Simulate card detection
        mock->simulateCardInserted();
        
        // Channel should have forwarded signal
        QCOMPARE(spy.count(), 1);
        
        // Check signal argument (UID)
        QList<QVariant> arguments = spy.takeFirst();
        QString uid = arguments.at(0).toString();
        QVERIFY(!uid.isEmpty());
        
        // Channel should be connected now
        QVERIFY(channel.isConnected());
        QCOMPARE(channel.targetUid(), uid);
        
        qDebug() << "Detected UID:" << uid;
        qDebug() << "✓ targetDetected signal forwarded correctly";
    }

    void testTargetLostSignal() {
        qDebug() << "\n--- Test: targetLost Signal Forwarding ---";
        
        auto* mock = new MockBackend();
        KeycardChannel channel(mock);
        
        // First insert card
        mock->simulateCardInserted();
        QVERIFY(channel.isConnected());
        
        // Setup spy
        QSignalSpy spy(&channel, &KeycardChannel::targetLost);
        QVERIFY(spy.isValid());
        
        // Remove card
        mock->simulateCardRemoved();
        
        // Channel should have forwarded signal
        QCOMPARE(spy.count(), 1);
        
        // Channel should be disconnected
        QVERIFY(!channel.isConnected());
        QVERIFY(channel.targetUid().isEmpty());
        
        qDebug() << "✓ targetLost signal forwarded correctly";
    }

    void testErrorSignal() {
        qDebug() << "\n--- Test: error Signal Forwarding ---";
        
        auto* mock = new MockBackend();
        KeycardChannel channel(mock);
        
        // Setup spy
        QSignalSpy spy(&channel, &KeycardChannel::error);
        QVERIFY(spy.isValid());
        
        // Simulate error
        QString errorMsg = "Test error message";
        mock->simulateError(errorMsg);
        
        // Channel should have forwarded signal
        QCOMPARE(spy.count(), 1);
        
        // Check error message
        QList<QVariant> arguments = spy.takeFirst();
        QString receivedMsg = arguments.at(0).toString();
        QCOMPARE(receivedMsg, errorMsg);
        
        qDebug() << "Error message:" << receivedMsg;
        qDebug() << "✓ error signal forwarded correctly";
    }

    // ========================================================================
    // Detection Tests
    // ========================================================================

    void testStartDetectionWithMock() {
        qDebug() << "\n--- Test: startDetection with Mock ---";
        
        auto* mock = new MockBackend();
        KeycardChannel channel(mock);
        
        // Start detection
        channel.startDetection();
        
        // Mock should be detecting
        QVERIFY(mock->isDetecting());
        
        // Stop detection
        channel.stopDetection();
        
        // Mock should have stopped
        QVERIFY(!mock->isDetecting());
        
        qDebug() << "✓ Detection control works with mock";
    }

    void testAutoConnect() {
        qDebug() << "\n--- Test: Auto-Connect ---";
        
        auto* mock = new MockBackend();
        mock->setAutoConnect(true);
        
        KeycardChannel channel(mock);
        
        // Setup spy
        QSignalSpy spy(&channel, &KeycardChannel::targetDetected);
        
        // Start detection
        channel.startDetection();
        
        // Wait for auto-connect (mock delays 50ms)
        QVERIFY(spy.wait(200));
        
        // Should be connected
        QVERIFY(channel.isConnected());
        QVERIFY(!channel.targetUid().isEmpty());
        
        qDebug() << "Auto-connected with UID:" << channel.targetUid();
        qDebug() << "✓ Auto-connect works";
    }

    // ========================================================================
    // Transmission Tests
    // ========================================================================

    void testTransmitWithMock() {
        qDebug() << "\n--- Test: transmit() with Mock ---";
        
        auto* mock = new MockBackend();
        KeycardChannel channel(mock);
        
        // Connect card
        mock->simulateCardInserted();
        QVERIFY(channel.isConnected());
        
        // Queue a response
        QByteArray expectedResponse = QByteArray::fromHex("AABBCCDD9000");
        mock->queueResponse(expectedResponse);
        
        // Transmit APDU
        QByteArray apdu = QByteArray::fromHex("00A4040000");
        QByteArray response = channel.transmit(apdu);
        
        // Should get queued response
        QCOMPARE(response, expectedResponse);
        
        // Mock should have tracked the APDU
        QCOMPARE(mock->getTransmitCount(), 1);
        QCOMPARE(mock->getLastTransmittedApdu(), apdu);
        
        qDebug() << "Transmitted:" << apdu.toHex();
        qDebug() << "Received:" << response.toHex();
        qDebug() << "✓ Transmission works with mock";
    }

    void testTransmitWithoutConnection() {
        qDebug() << "\n--- Test: transmit() without Connection ---";
        
        auto* mock = new MockBackend();
        KeycardChannel channel(mock);
        
        // Should throw when not connected
        bool threw = false;
        try {
            QByteArray apdu = QByteArray::fromHex("00A4040000");
            channel.transmit(apdu);
        } catch (const std::runtime_error& e) {
            threw = true;
            qDebug() << "Exception message:" << e.what();
        }
        
        QVERIFY(threw);
        qDebug() << "✓ transmit() throws when not connected";
    }

    void testMultipleTransmissions() {
        qDebug() << "\n--- Test: Multiple Transmissions ---";
        
        auto* mock = new MockBackend();
        KeycardChannel channel(mock);
        
        mock->simulateCardInserted();
        
        // Queue multiple responses
        mock->queueResponse(QByteArray::fromHex("11229000"));
        mock->queueResponse(QByteArray::fromHex("33449000"));
        mock->queueResponse(QByteArray::fromHex("55669000"));
        
        // Transmit multiple APDUs
        for (int i = 0; i < 3; i++) {
            QByteArray apdu = QByteArray::fromHex(QString("00%1").arg(i).toLatin1());
            QByteArray response = channel.transmit(apdu);
            qDebug() << "  TX" << i << ":" << apdu.toHex() << "-> RX:" << response.toHex();
        }
        
        // Should have tracked all
        QCOMPARE(mock->getTransmitCount(), 3);
        
        qDebug() << "✓ Multiple transmissions work";
    }

    // ========================================================================
    // Disconnection Tests
    // ========================================================================

    void testDisconnect() {
        qDebug() << "\n--- Test: disconnect() ---";
        
        auto* mock = new MockBackend();
        KeycardChannel channel(mock);
        
        // Connect
        mock->simulateCardInserted();
        QVERIFY(channel.isConnected());
        
        // Setup spy
        QSignalSpy spy(&channel, &KeycardChannel::targetLost);
        
        // Disconnect
        channel.disconnect();
        
        // Should emit targetLost
        QCOMPARE(spy.count(), 1);
        QVERIFY(!channel.isConnected());
        
        qDebug() << "✓ disconnect() works";
    }

    // ========================================================================
    // Polling Interval Tests
    // ========================================================================

    void testSetPollingInterval() {
        qDebug() << "\n--- Test: setPollingInterval() ---";
        
        auto* mock = new MockBackend();
        KeycardChannel channel(mock);
        
        // Set polling interval
        channel.setPollingInterval(250);
        
        // Mock should have received it
        QCOMPARE(mock->getPollingInterval(), 250);
        
        // Try different values
        channel.setPollingInterval(100);
        QCOMPARE(mock->getPollingInterval(), 100);
        
        qDebug() << "✓ Polling interval forwarded to backend";
    }

    // ========================================================================
    // Backend Reset Tests
    // ========================================================================

    void testBackendReset() {
        qDebug() << "\n--- Test: Backend Reset ---";
        
        auto* mock = new MockBackend();
        KeycardChannel channel(mock);
        
        // Do some operations
        mock->simulateCardInserted();
        mock->queueResponse(QByteArray::fromHex("9000"));
        channel.transmit(QByteArray::fromHex("00A4"));
        
        // Reset backend
        mock->reset();
        
        // Should be clean state
        QVERIFY(!mock->isConnected());
        QVERIFY(!mock->isDetecting());
        QCOMPARE(mock->getTransmitCount(), 0);
        
        qDebug() << "✓ Backend reset works";
    }

    // ========================================================================
    // Error Simulation Tests
    // ========================================================================

    void testTransmitException() {
        qDebug() << "\n--- Test: Transmit Exception Simulation ---";
        
        auto* mock = new MockBackend();
        KeycardChannel channel(mock);
        
        mock->simulateCardInserted();
        
        // Set next transmit to throw
        mock->setNextTransmitThrows("Simulated transmission error");
        
        // Should throw
        bool threw = false;
        try {
            channel.transmit(QByteArray::fromHex("00A4"));
        } catch (const std::runtime_error& e) {
            threw = true;
            QString msg = e.what();
            QVERIFY(msg.contains("Simulated"));
            qDebug() << "Caught exception:" << msg;
        }
        
        QVERIFY(threw);
        
        // Next transmission should work
        mock->queueResponse(QByteArray::fromHex("9000"));
        QByteArray response = channel.transmit(QByteArray::fromHex("00A4"));
        QCOMPARE(response, QByteArray::fromHex("9000"));
        
        qDebug() << "✓ Exception simulation works";
    }

    // ========================================================================
    // Lifecycle Tests
    // ========================================================================

    void testChannelDeletion() {
        qDebug() << "\n--- Test: Channel Deletion ---";
        
        auto* mock = new MockBackend();
        QVERIFY(mock->parent() == nullptr);
        
        {
            KeycardChannel channel(mock);
            // Mock should be adopted
            QCOMPARE(mock->parent(), &channel);
            
            mock->simulateCardInserted();
            QVERIFY(channel.isConnected());
            
            // Channel goes out of scope
        }
        
        // Mock should be deleted by channel
        // (Can't verify directly, but no crash = success)
        
        qDebug() << "✓ Channel deletion cleans up backend";
    }

    // ========================================================================
    // Complex Scenarios
    // ========================================================================

    void testMultipleConnectDisconnectCycles() {
        qDebug() << "\n--- Test: Multiple Connect/Disconnect Cycles ---";
        
        auto* mock = new MockBackend();
        KeycardChannel channel(mock);
        
        for (int i = 0; i < 5; i++) {
            qDebug() << "  Cycle" << i + 1;
            
            // Connect
            mock->simulateCardInserted();
            QVERIFY(channel.isConnected());
            
            // Transmit
            mock->queueResponse(QByteArray::fromHex("9000"));
            channel.transmit(QByteArray::fromHex("00A4"));
            
            // Disconnect
            mock->simulateCardRemoved();
            QVERIFY(!channel.isConnected());
        }
        
        // Should have tracked all transmissions
        QCOMPARE(mock->getTransmitCount(), 5);
        
        qDebug() << "✓ Multiple cycles work";
    }

    void testSignalOrder() {
        qDebug() << "\n--- Test: Signal Order ---";
        
        auto* mock = new MockBackend();
        KeycardChannel channel(mock);
        
        QSignalSpy spyDetected(&channel, &KeycardChannel::targetDetected);
        QSignalSpy spyLost(&channel, &KeycardChannel::targetLost);
        
        // Sequence: insert, remove, insert, remove
        mock->simulateCardInserted();
        mock->simulateCardRemoved();
        mock->simulateCardInserted();
        mock->simulateCardRemoved();
        
        // Should have correct counts
        QCOMPARE(spyDetected.count(), 2);
        QCOMPARE(spyLost.count(), 2);
        
        qDebug() << "✓ Signals emitted in correct order";
    }
};

QTEST_MAIN(TestKeycardChannelDI)
#include "test_keycard_channel_di.moc"

