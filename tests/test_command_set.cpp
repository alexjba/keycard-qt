/**
 * Unit tests for CommandSet
 */

#include <QTest>
#include <QDebug>
#include "keycard-qt/command_set.h"
#include "keycard-qt/channel_interface.h"

// Mock channel for testing
class MockChannel : public Keycard::IChannel {
public:
    QByteArray lastTransmitted;
    QByteArray nextResponse;
    bool connected = true;
    
    QByteArray transmit(const QByteArray& apdu) override {
        lastTransmitted = apdu;
        return nextResponse;
    }
    
    bool isConnected() const override {
        return connected;
    }
};

class TestCommandSet : public QObject {
    Q_OBJECT
    
private slots:
    void initTestCase() {
        qDebug() << "Testing CommandSet";
    }
    
    void testConstruction() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Should construct successfully
        QVERIFY(cmd.lastError().isEmpty());
    }
    
    void testSelectCommand() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Mock response for SELECT (pre-initialized card with public key)
        QByteArray mockResponse = QByteArray::fromHex(
            "80"  // TAG: Pre-initialized
            "41"  // Length: 65 bytes
        );
        // Add 65-byte public key
        mockResponse.append(QByteArray(65, 0x04));
        mockResponse.append(QByteArray::fromHex("9000")); // SW OK
        
        mockChannel.nextResponse = mockResponse;
        
        Keycard::ApplicationInfo info = cmd.select();
        
        // Verify SELECT command was sent
        QVERIFY(!mockChannel.lastTransmitted.isEmpty());
        QCOMPARE((uint8_t)mockChannel.lastTransmitted[0], (uint8_t)0x00); // CLA
        QCOMPARE((uint8_t)mockChannel.lastTransmitted[1], (uint8_t)0xA4); // INS SELECT
        
        // Verify ApplicationInfo
        QVERIFY(info.installed);
    }
    
    void testSelectError() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Mock error response
        mockChannel.nextResponse = QByteArray::fromHex("6A82"); // File not found
        
        Keycard::ApplicationInfo info = cmd.select();
        
        // Should have error
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testVerifyPINWithoutSecureChannel() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Try to verify PIN without opening secure channel
        bool result = cmd.verifyPIN("000000");
        
        // Should fail (secure channel not open)
        QVERIFY(!result);
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void cleanupTestCase() {
        qDebug() << "CommandSet tests complete";
    }
};

QTEST_MAIN(TestCommandSet)
#include "test_command_set.moc"

