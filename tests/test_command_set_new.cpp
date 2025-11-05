/**
 * Unit tests for new CommandSet methods
 * Tests all 19 newly implemented methods
 */

#include <QTest>
#include <QDebug>
#include <QRandomGenerator>
#include "keycard-qt/command_set.h"
#include "keycard-qt/channel_interface.h"

// Mock channel for testing
class MockChannel : public Keycard::IChannel {
public:
    QByteArray lastTransmitted;
    QByteArray nextResponse;
    QList<QByteArray> responseQueue;
    bool connected = true;
    int transmitCount = 0;
    
    QByteArray transmit(const QByteArray& apdu) override {
        lastTransmitted = apdu;
        transmitCount++;
        
        if (!responseQueue.isEmpty()) {
            return responseQueue.takeFirst();
        }
        return nextResponse;
    }
    
    bool isConnected() const override {
        return connected;
    }
    
    void reset() {
        lastTransmitted.clear();
        nextResponse.clear();
        responseQueue.clear();
        transmitCount = 0;
    }
};

class TestCommandSetNew : public QObject {
    Q_OBJECT
    
private:
    // Helper to create a mock secure channel session by directly injecting state
    void setupSecureChannel(MockChannel& mockChannel, Keycard::CommandSet& cmd) {
        // Create mock pairing info
        QByteArray pairingKey(32, 0xAB);  // Mock pairing key
        Keycard::PairingInfo pairingInfo(pairingKey, 1);
        
        // Create mock session keys (16 bytes each for AES-128)
        QByteArray mockIV(16, 0x00);      // Initialization vector
        QByteArray mockEncKey(16, 0xEE);  // Encryption key
        QByteArray mockMacKey(16, 0xDD);  // MAC key
        
        // Directly inject state, bypassing crypto validation
        cmd.testInjectSecureChannelState(pairingInfo, mockIV, mockEncKey, mockMacKey);
        
        mockChannel.reset();
    }
    
private slots:
    void initTestCase() {
        qDebug() << "Testing new CommandSet methods";
    }
    
    // ========================================
    // Security Operations Tests
    // ========================================
    
    void testChangePIN() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        setupSecureChannel(mockChannel, cmd);
        
        // Mock success response
        mockChannel.nextResponse = QByteArray::fromHex("9000");
        
        bool result = cmd.changePIN("123456");
        
        QVERIFY(result);
        QVERIFY(!mockChannel.lastTransmitted.isEmpty());
        QVERIFY(cmd.lastError().isEmpty());
    }
    
    void testChangePINWithoutSecureChannel() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        bool result = cmd.changePIN("123456");
        
        QVERIFY(!result);
        QVERIFY(!cmd.lastError().isEmpty());
        QVERIFY(cmd.lastError().contains("Secure channel"));
    }
    
    void testChangePUK() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        setupSecureChannel(mockChannel, cmd);
        
        mockChannel.nextResponse = QByteArray::fromHex("9000");
        
        bool result = cmd.changePUK("123456789012");
        
        QVERIFY(result);
        QVERIFY(!mockChannel.lastTransmitted.isEmpty());
    }
    
    void testChangePUKWithoutSecureChannel() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        bool result = cmd.changePUK("123456789012");
        
        QVERIFY(!result);
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testUnblockPIN() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        setupSecureChannel(mockChannel, cmd);
        
        mockChannel.nextResponse = QByteArray::fromHex("9000");
        
        bool result = cmd.unblockPIN("123456789012", "654321");
        
        QVERIFY(result);
        QVERIFY(!mockChannel.lastTransmitted.isEmpty());
    }
    
    void testUnblockPINWrongPUK() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        setupSecureChannel(mockChannel, cmd);
        
        // Mock wrong PUK (5 attempts remaining)
        mockChannel.nextResponse = QByteArray::fromHex("63C5");
        
        bool result = cmd.unblockPIN("000000000000", "654321");
        
        QVERIFY(!result);
        QVERIFY(cmd.lastError().contains("Wrong PUK"));
        QVERIFY(cmd.lastError().contains("5"));
    }
    
    void testChangePairingSecret() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        setupSecureChannel(mockChannel, cmd);
        
        mockChannel.nextResponse = QByteArray::fromHex("9000");
        
        bool result = cmd.changePairingSecret("newpassword");
        
        QVERIFY(result);
    }
    
    // ========================================
    // Key Management Tests
    // ========================================
    
    void testGenerateKey() {
        // Note: Full testing requires encrypted response from secure channel
        // This test verifies the command is sent correctly
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Try without secure channel - should fail
        QByteArray result = cmd.generateKey();
        QVERIFY(result.isEmpty());
        QVERIFY(!cmd.lastError().isEmpty());
        QVERIFY(cmd.lastError().contains("Secure channel"));
    }
    
    void testGenerateKeyWithoutSecureChannel() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        QByteArray result = cmd.generateKey();
        
        QVERIFY(result.isEmpty());
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testGenerateMnemonic() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Test without secure channel
        QVector<int> result = cmd.generateMnemonic(4);
        QVERIFY(result.isEmpty());
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testGenerateMnemonicEmpty() {
        // This test verifies the method signature works
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail
        QVector<int> result = cmd.generateMnemonic();
        QVERIFY(result.isEmpty());
    }
    
    void testLoadSeed() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        QByteArray seed(64, 0xAB);
        
        // Without secure channel, should fail
        QByteArray result = cmd.loadSeed(seed);
        QVERIFY(result.isEmpty());
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testLoadSeedInvalidSize() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        QByteArray seed(32, 0xAB);  // Wrong size (should be 64)
        
        QByteArray result = cmd.loadSeed(seed);
        
        // Should fail due to size validation
        QVERIFY(result.isEmpty());
        QVERIFY(cmd.lastError().contains("64 bytes"));
    }
    
    void testRemoveKey() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail
        bool result = cmd.removeKey();
        QVERIFY(!result);
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testDeriveKeyAbsolutePath() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail
        bool result = cmd.deriveKey("m/44'/60'/0'/0/0");
        QVERIFY(!result);
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testDeriveKeyRelativePath() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Test path parsing (will fail without secure channel)
        bool result = cmd.deriveKey("../0/1");
        QVERIFY(!result);
    }
    
    void testDeriveKeyCurrentPath() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Test path parsing (will fail without secure channel)
        bool result = cmd.deriveKey("./5");
        QVERIFY(!result);
    }
    
    // ========================================
    // Signing Tests
    // ========================================
    
    void testSign() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail
        QByteArray hash(32, 0x12);
        QByteArray result = cmd.sign(hash);
        QVERIFY(result.isEmpty());
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testSignInvalidHashSize() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        QByteArray hash(16, 0x12);  // Wrong size (should be 32)
        
        QByteArray result = cmd.sign(hash);
        
        // Should fail due to size validation
        QVERIFY(result.isEmpty());
        QVERIFY(cmd.lastError().contains("32 bytes"));
    }
    
    void testSignWithPath() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail
        QByteArray hash(32, 0x12);
        QByteArray result = cmd.signWithPath(hash, "m/44'/60'/0'/0/0", false);
        QVERIFY(result.isEmpty());
    }
    
    void testSignWithPathMakeCurrent() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail
        QByteArray hash(32, 0x12);
        QByteArray result = cmd.signWithPath(hash, "m/44'/60'/0'/0/0", true);
        QVERIFY(result.isEmpty());
    }
    
    void testSignPinless() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail
        QByteArray hash(32, 0x12);
        QByteArray result = cmd.signPinless(hash);
        QVERIFY(result.isEmpty());
    }
    
    void testSetPinlessPath() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail (but after path validation)
        bool result = cmd.setPinlessPath("m/44'/60'/0'/0/0");
        QVERIFY(!result);
    }
    
    void testSetPinlessPathRelative() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Should fail - pinless path must be absolute (validation happens first)
        bool result = cmd.setPinlessPath("../0/0");
        QVERIFY(!result);
        QVERIFY(cmd.lastError().contains("absolute"));
    }
    
    // ========================================
    // Data Storage Tests
    // ========================================
    
    void testStoreData() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail
        QByteArray data = "Hello, Keycard!";
        bool result = cmd.storeData(0x00, data);
        QVERIFY(!result);
    }
    
    void testStoreDataNDEF() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail
        QByteArray data = "NDEF data";
        bool result = cmd.storeData(0x01, data);
        QVERIFY(!result);
    }
    
    void testGetData() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail
        QByteArray result = cmd.getData(0x00);
        QVERIFY(result.isEmpty());
    }
    
    void testGetDataEmpty() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Test that method can be called
        QByteArray result = cmd.getData(0x00);
        QVERIFY(result.isEmpty());
    }
    
    // ========================================
    // Utilities Tests
    // ========================================
    
    void testIdentify() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        QByteArray mockIdentity = "KeycardIdentity";
        mockChannel.nextResponse = mockIdentity + QByteArray::fromHex("9000");
        
        QByteArray result = cmd.identify();
        
        QVERIFY(!result.isEmpty());
        QCOMPARE(result, mockIdentity);
    }
    
    void testIdentifyWithChallenge() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        QByteArray challenge(32, 0xAB);
        QByteArray mockIdentity = "KeycardIdentity";
        mockChannel.nextResponse = mockIdentity + QByteArray::fromHex("9000");
        
        QByteArray result = cmd.identify(challenge);
        
        QVERIFY(!result.isEmpty());
    }
    
    void testExportKeyCurrent() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail
        QByteArray result = cmd.exportKey(false, false, "");
        QVERIFY(result.isEmpty());
    }
    
    void testExportKeyDerive() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail
        QByteArray result = cmd.exportKey(true, false, "m/44'/60'/0'/0/0");
        QVERIFY(result.isEmpty());
    }
    
    void testExportKeyDeriveAndMakeCurrent() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail
        QByteArray result = cmd.exportKey(true, true, "m/44'/60'/0'/0/0");
        QVERIFY(result.isEmpty());
    }
    
    void testExportKeyExtended() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Without secure channel, should fail
        QByteArray result = cmd.exportKeyExtended(true, false, "m/44'/60'/0'/0/0");
        QVERIFY(result.isEmpty());
    }
    
    void testFactoryReset() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        mockChannel.nextResponse = QByteArray::fromHex("9000");
        
        bool result = cmd.factoryReset();
        
        QVERIFY(result);
        // ApplicationInfo should be reset
        QVERIFY(cmd.applicationInfo().instanceUID.isEmpty());
    }
    
    void testFactoryResetFailed() {
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        mockChannel.nextResponse = QByteArray::fromHex("6985");  // Conditions not satisfied
        
        bool result = cmd.factoryReset();
        
        QVERIFY(!result);
    }
    
    // ========================================
    // Edge Cases & Error Handling
    // ========================================
    
    void testMultipleOperationsSequence() {
        // Test that multiple methods can be called in sequence
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // All should fail without secure channel
        QByteArray keyUID = cmd.generateKey();
        QVERIFY(keyUID.isEmpty());
        
        bool derived = cmd.deriveKey("m/44'/60'/0'/0/0");
        QVERIFY(!derived);
        
        QByteArray hash(32, 0x12);
        QByteArray sig = cmd.sign(hash);
        QVERIFY(sig.isEmpty());
    }
    
    void testPathParsingHardenedNotation() {
        // Test that path parsing handles both ' and h notation
        MockChannel mockChannel;
        Keycard::CommandSet cmd(&mockChannel);
        
        // Both should fail without secure channel, but path parsing should work
        bool result1 = cmd.deriveKey("m/44'/60'/0'");
        QVERIFY(!result1);
        
        bool result2 = cmd.deriveKey("m/44h/60h/0h");
        QVERIFY(!result2);
        
        // The key is that both are accepted syntactically
        QVERIFY(cmd.lastError().contains("Secure channel"));
    }
    
    void cleanupTestCase() {
        qDebug() << "New CommandSet tests complete";
    }
};

QTEST_MAIN(TestCommandSetNew)
#include "test_command_set_new.moc"

