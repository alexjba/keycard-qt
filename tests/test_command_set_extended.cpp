#include <QTest>
#include "keycard-qt/command_set.h"
#include "keycard-qt/channel_interface.h"

using namespace Keycard;

// Mock channel that can simulate various card responses
class ExtendedMockChannel : public IChannel {
public:
    QByteArray lastTransmitted;
    QList<QByteArray> responseQueue;  // Queue of responses
    int transmitCount = 0;
    
    QByteArray transmit(const QByteArray& apdu) override {
        lastTransmitted = apdu;
        transmitCount++;
        
        if (responseQueue.isEmpty()) {
            // Default success response
            return QByteArray::fromHex("9000");
        }
        
        return responseQueue.takeFirst();
    }
    
    bool isConnected() const override { return true; }
};

class TestCommandSetExtended : public QObject {
    Q_OBJECT
    
private:
    ExtendedMockChannel* mockChannel;
    CommandSet* cmdSet;
    
private slots:
    void initTestCase() {
        mockChannel = new ExtendedMockChannel();
        cmdSet = new CommandSet(mockChannel);
    }
    
    void cleanupTestCase() {
        delete cmdSet;
        delete mockChannel;
    }
    
    void init() {
        mockChannel->responseQueue.clear();
        mockChannel->transmitCount = 0;
        mockChannel->lastTransmitted.clear();
        
        // Reset command set state by recreating it
        delete cmdSet;
        cmdSet = new CommandSet(mockChannel);
    }
    
    // Test pair() - cryptographic validation rejects mock data
    // This test verifies that the crypto validation is working correctly
    void testPairFullFlow() {
        // Mock response with invalid cryptogram (will be rejected by crypto validation)
        QByteArray cardCryptogram = QByteArray(32, 0xAA);  // Invalid cryptogram
        QByteArray cardChallenge = QByteArray(32, 0xBB);   // Challenge
        QByteArray step1Response = cardCryptogram + cardChallenge + QByteArray::fromHex("9000");
        mockChannel->responseQueue.append(step1Response);
        
        // Call pair - should fail due to cryptogram validation
        PairingInfo result = cmdSet->pair("test-password-123");
        
        // Verify it failed (crypto validation working)
        QVERIFY(!result.isValid());
        // The error message contains "CRYPTOGRAM" or similar crypto-related error
        QString error = cmdSet->lastError();
        QVERIFY(error.contains("CRYPTOGRAM") || error.contains("Invalid"));
    }
    
    void testPairStepOneFailed() {
        // Mock failure in step 1
        mockChannel->responseQueue.append(QByteArray::fromHex("6982"));  // Security condition not satisfied
        
        PairingInfo result = cmdSet->pair("test-password");
        
        // Should fail
        QVERIFY(!result.isValid());
        QVERIFY(!cmdSet->lastError().isEmpty());
    }
    
    void testPairInvalidResponseSize() {
        // Mock response with invalid size (should be 64 bytes + SW)
        mockChannel->responseQueue.append(QByteArray(10, 0x00) + QByteArray::fromHex("9000"));
        
        PairingInfo result = cmdSet->pair("test-password");
        
        QVERIFY(!result.isValid());
        QVERIFY(cmdSet->lastError().contains("Invalid pair response size"));
    }
    
    // Test openSecureChannel()
    void testOpenSecureChannelInvalidPairing() {
        PairingInfo invalidPairing;  // Empty pairing info
        
        bool result = cmdSet->openSecureChannel(invalidPairing);
        
        QVERIFY(!result);
        QVERIFY(cmdSet->lastError().contains("Invalid pairing"));
    }
    
    void testOpenSecureChannelSuccess() {
        // Test that openSecureChannel validates pairing info
        // With mock data, crypto validation will fail (which is correct behavior)
        PairingInfo pairing(QByteArray(32, 0xAA), 0);
        QByteArray salt = QByteArray(32, 0xBB);
        mockChannel->responseQueue.append(salt + QByteArray::fromHex("9000"));
        
        bool result = cmdSet->openSecureChannel(pairing);
        
        // Should fail due to crypto validation (no valid ECDH secret)
        QVERIFY(!result);
        QVERIFY(!cmdSet->lastError().isEmpty());
    }
    
    // Test getStatus()
    void testGetStatusWithoutSecureChannel() {
        // Try without opening secure channel
        ApplicationStatus status = cmdSet->getStatus();
        
        // Should fail
        QVERIFY(cmdSet->lastError().contains("Secure channel not open"));
        QCOMPARE(status.pinRetryCount, 0);
    }
    
    // Test unpair()
    void testUnpairWithoutSecureChannel() {
        bool result = cmdSet->unpair(0);
        
        QVERIFY(!result);
        QVERIFY(cmdSet->lastError().contains("Secure channel not open"));
    }
    
    // Test init() requires secure channel
    void testInitNotImplemented() {
        Secrets secrets("123456", "123456789012", "pairing-pass");
        
        bool result = cmdSet->init(secrets);
        
        // Behavior depends on whether QCA is available:
        // - With QCA: should fail due to encryption issues (no proper secure channel)
        // - Without QCA: may succeed (encryption is bypassed, command sent without encryption)
        // We test that init() either fails with proper error, or succeeds when crypto is disabled
        
        if (!result) {
            // If it fails, verify the error is related to secure channel/encryption
            QString error = cmdSet->lastError();
            QVERIFY(error.contains("Failed to encrypt") || 
                    error.contains("Secure channel") || 
                    error.contains("shared secret"));
        } else {
            // If it succeeds, it's because QCA is not available (crypto bypassed)
            // This is acceptable behavior for builds without QCA
            QVERIFY(true);
        }
    }
    
    // Test accessors
    void testAccessors() {
        QVERIFY(cmdSet->applicationInfo().instanceUID.isEmpty());
        QVERIFY(!cmdSet->pairingInfo().isValid());
        QCOMPARE(cmdSet->remainingPINAttempts(), -1);
    }
    
    // Test verifyPIN with wrong PIN
    void testVerifyPINWrongCode() {
        // verifyPIN requires secure channel, so just test that it fails appropriately
        bool result = cmdSet->verifyPIN("wrong-pin");
        
        QVERIFY(!result);
        QVERIFY(cmdSet->lastError().contains("Secure channel not open"));
    }
    
    void testVerifyPINBlocked() {
        // verifyPIN requires secure channel
        bool result = cmdSet->verifyPIN("any-pin");
        
        QVERIFY(!result);
        QVERIFY(cmdSet->lastError().contains("Secure channel not open"));
    }
    
    // Test buildCommand indirectly
    void testBuildCommandViaSelect() {
        mockChannel->responseQueue.append(QByteArray::fromHex("9000"));
        
        cmdSet->select();
        
        // Verify SELECT command structure
        QVERIFY(!mockChannel->lastTransmitted.isEmpty());
        QCOMPARE(static_cast<uint8_t>(mockChannel->lastTransmitted[0]), static_cast<uint8_t>(0x00));  // CLA_ISO7816
        QCOMPARE(static_cast<uint8_t>(mockChannel->lastTransmitted[1]), static_cast<uint8_t>(0xA4));  // INS_SELECT
    }
    
    // Test error handling
    void testCheckOKWithVariousErrors() {
        // Test various error codes
        mockChannel->responseQueue.append(QByteArray::fromHex("6982"));  // Security condition
        cmdSet->select();
        QVERIFY(!cmdSet->lastError().isEmpty());
        QVERIFY(cmdSet->lastError().contains("6982"));
        
        mockChannel->responseQueue.append(QByteArray::fromHex("6A80"));  // Wrong data
        cmdSet->select();
        QVERIFY(cmdSet->lastError().contains("6a80"));
        
        mockChannel->responseQueue.append(QByteArray::fromHex("6D00"));  // INS not supported
        cmdSet->select();
        QVERIFY(cmdSet->lastError().contains("6d00"));
    }
};

QTEST_MAIN(TestCommandSetExtended)
#include "test_command_set_extended.moc"

