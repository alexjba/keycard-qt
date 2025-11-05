/**
 * Unit tests for INIT and PAIR commands
 * 
 * Tests the card initialization and pairing flow
 */

#include <QTest>
#include <QDebug>
#include "keycard-qt/command_set.h"
#include "keycard-qt/types.h"
#include "keycard-qt/apdu/command.h"
#include "keycard-qt/apdu/response.h"

using namespace Keycard;

// Mock channel for testing
class MockChannel : public IChannel {
public:
    QByteArray transmit(const QByteArray& apdu) override {
        lastApdu = apdu;
        transmitCount++;
        
        if (mockResponse.isEmpty()) {
            // Default success response
            return QByteArray::fromHex("9000");
        }
        return mockResponse;
    }
    
    bool isConnected() const override {
        return connected;
    }
    
    QByteArray lastApdu;
    QByteArray mockResponse;
    int transmitCount = 0;
    bool connected = true;
};

class TestInitPair : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qDebug() << "Testing INIT and PAIR commands";
    }

    // ========== INIT Command Tests ==========
    
    void testInitValidSecrets() {
        qDebug() << "Test: INIT validates secrets format";
        
        // Test that valid secrets pass validation
        Secrets secrets("123456", "123456789012", "KeycardTest");
        QCOMPARE(secrets.pin.length(), 6);
        QCOMPARE(secrets.puk.length(), 12);
        QVERIFY(secrets.pairingPassword.length() >= 5);
        
        qDebug() << "   ✅ Valid secrets format accepted";
    }
    
    void testInitInvalidPIN() {
        qDebug() << "Test: INIT with invalid PIN";
        
        MockChannel channel;
        CommandSet cmdSet(&channel);
        
        // Mock SELECT response (pre-initialized)
        QByteArray pubkey(65, 0xAA);
        pubkey[0] = 0x04;
        channel.mockResponse = QByteArray::fromHex("8041") + pubkey + QByteArray::fromHex("9000");
        cmdSet.select();
        
        // Test PIN too short
        Secrets secrets1("12345", "123456789012", "KeycardTest");
        QVERIFY(!cmdSet.init(secrets1));
        QVERIFY(cmdSet.lastError().contains("PIN must be 6 digits"));
        
        // Test PIN too long
        Secrets secrets2("1234567", "123456789012", "KeycardTest");
        QVERIFY(!cmdSet.init(secrets2));
        QVERIFY(cmdSet.lastError().contains("PIN must be 6 digits"));
        
        qDebug() << "   ✅ Rejects invalid PIN lengths";
    }
    
    void testInitInvalidPUK() {
        qDebug() << "Test: INIT with invalid PUK";
        
        MockChannel channel;
        CommandSet cmdSet(&channel);
        
        // Mock SELECT response (pre-initialized)
        QByteArray pubkey(65, 0xAA);
        pubkey[0] = 0x04;
        channel.mockResponse = QByteArray::fromHex("8041") + pubkey + QByteArray::fromHex("9000");
        cmdSet.select();
        
        // Test PUK too short
        Secrets secrets1("123456", "12345678901", "KeycardTest");
        QVERIFY(!cmdSet.init(secrets1));
        QVERIFY(cmdSet.lastError().contains("PUK must be 12 digits"));
        
        // Test PUK too long
        Secrets secrets2("123456", "1234567890123", "KeycardTest");
        QVERIFY(!cmdSet.init(secrets2));
        QVERIFY(cmdSet.lastError().contains("PUK must be 12 digits"));
        
        qDebug() << "   ✅ Rejects invalid PUK lengths";
    }
    
    void testInitInvalidPairingPassword() {
        qDebug() << "Test: INIT with invalid pairing password";
        
        MockChannel channel;
        CommandSet cmdSet(&channel);
        
        // Mock SELECT response (pre-initialized)
        QByteArray pubkey(65, 0xAA);
        pubkey[0] = 0x04;
        channel.mockResponse = QByteArray::fromHex("8041") + pubkey + QByteArray::fromHex("9000");
        cmdSet.select();
        
        // Test password too short
        Secrets secrets("123456", "123456789012", "abc");
        QVERIFY(!cmdSet.init(secrets));
        QVERIFY(cmdSet.lastError().contains("at least 5 characters"));
        
        qDebug() << "   ✅ Rejects short pairing password";
    }
    
    void testInitAPDUFormat() {
        qDebug() << "Test: INIT requires pre-initialized card";
        
        MockChannel channel;
        CommandSet cmdSet(&channel);
        
        // Mock SELECT response (INITIALIZED card - wrong state for INIT)
        channel.mockResponse = QByteArray::fromHex("A4") +  // Application info template
                               QByteArray::fromHex("17") +  // Length
                               QByteArray::fromHex("8F10") + QByteArray(16, 0xAA) +  // Instance UID
                               QByteArray::fromHex("8040") + QByteArray(65, 0xBB) +  // Public key
                               QByteArray::fromHex("9000");
        cmdSet.select();
        
        // Try to INIT an already-initialized card (should fail or skip)
        channel.mockResponse = QByteArray::fromHex("6985");  // Conditions not satisfied
        
        Secrets secrets("123456", "123456789012", "password");
        bool result = cmdSet.init(secrets);
        
        // Should fail (card already initialized)
        QVERIFY(!result);
        
        qDebug() << "   ✅ INIT requires pre-initialized card";
    }
    
    void testInitEncryption() {
        qDebug() << "Test: INIT data encryption";
        
        MockChannel channel;
        CommandSet cmdSet(&channel);
        
        // Mock SELECT response (pre-initialized)
        QByteArray pubkey(65, 0xAA);
        pubkey[0] = 0x04;
        channel.mockResponse = QByteArray::fromHex("8041") + pubkey + QByteArray::fromHex("9000");
        cmdSet.select();
        
        // Mock INIT response
        channel.mockResponse = QByteArray::fromHex("9000");
        
        Secrets secrets("123456", "123456789012", "password");
        cmdSet.init(secrets);
        
        // Verify data is encrypted (should not contain plaintext PIN/PUK)
        QByteArray apdu = channel.lastApdu;
        QString apduHex = apdu.toHex();
        
        // Should NOT contain plaintext "123456" or "123456789012"
        QVERIFY(!apduHex.contains("313233343536"));  // "123456" in hex
        QVERIFY(!apduHex.contains("313233343536373839303132"));  // "123456789012" in hex
        
        qDebug() << "   ✅ Secrets are encrypted";
    }

    // ========== PAIR Command Tests ==========
    
    void testPairBasicFlow() {
        qDebug() << "Test: PAIR basic flow";
        
        MockChannel channel;
        CommandSet cmdSet(&channel);
        
        // Mock SELECT response (initialized card)
        channel.mockResponse = QByteArray::fromHex("A4") +  // Application info template
                               QByteArray::fromHex("17") +  // Length
                               QByteArray::fromHex("8F10") + QByteArray(16, 0xAA) +  // Instance UID
                               QByteArray::fromHex("8040") + QByteArray(65, 0xBB) +  // Public key
                               QByteArray::fromHex("9000");
        cmdSet.select();
        
        // Mock PAIR step 1 response (card cryptogram + challenge)
        QByteArray cardCryptogram(32, 0xCC);
        QByteArray cardChallenge(32, 0xDD);
        channel.mockResponse = cardCryptogram + cardChallenge + QByteArray::fromHex("9000");
        
        // This will fail at cryptogram verification, but we can test the flow
        PairingInfo info = cmdSet.pair("KeycardTest");
        
        // Should have sent 1 PAIR command (step 1)
        QVERIFY(channel.transmitCount >= 2);  // SELECT + PAIR step 1
        
        qDebug() << "   ✅ PAIR flow initiated";
    }
    
    void testPairAPDUFormat() {
        qDebug() << "Test: PAIR APDU format";
        
        MockChannel channel;
        CommandSet cmdSet(&channel);
        
        // Mock SELECT
        channel.mockResponse = QByteArray::fromHex("A4") +
                               QByteArray::fromHex("17") +
                               QByteArray::fromHex("8F10") + QByteArray(16, 0xAA) +
                               QByteArray::fromHex("8040") + QByteArray(65, 0xBB) +
                               QByteArray::fromHex("9000");
        cmdSet.select();
        
        // Mock PAIR step 1 response
        channel.mockResponse = QByteArray(64, 0xCC) + QByteArray::fromHex("9000");
        cmdSet.pair("password");
        
        // Check PAIR step 1 APDU (CLA INS P1 P2 Lc Data...)
        QByteArray apdu = channel.lastApdu;
        
        QCOMPARE((uint8_t)apdu[0], (uint8_t)0x80);  // CLA
        QCOMPARE((uint8_t)apdu[1], (uint8_t)APDU::INS_PAIR);  // INS
        QCOMPARE((uint8_t)apdu[2], (uint8_t)APDU::P1PairFirstStep);  // P1
        
        // Data should be 32-byte challenge
        int dataLen = (uint8_t)apdu[4];
        QCOMPARE(dataLen, 32);
        
        qDebug() << "   ✅ PAIR APDU format correct";
    }
    
    void testPairDifferentPasswords() {
        qDebug() << "Test: PAIR with different passwords";
        
        MockChannel channel;
        CommandSet cmdSet(&channel);
        
        // Mock SELECT
        channel.mockResponse = QByteArray::fromHex("A4") +
                               QByteArray::fromHex("17") +
                               QByteArray::fromHex("8F10") + QByteArray(16, 0xAA) +
                               QByteArray::fromHex("8040") + QByteArray(65, 0xBB) +
                               QByteArray::fromHex("9000");
        cmdSet.select();
        
        // Test different passwords produce different challenges
        channel.mockResponse = QByteArray(64, 0xCC) + QByteArray::fromHex("9000");
        cmdSet.pair("password1");
        QByteArray apdu1 = channel.lastApdu;
        
        // Reset for second attempt
        cmdSet.select();
        channel.mockResponse = QByteArray(64, 0xCC) + QByteArray::fromHex("9000");
        cmdSet.pair("password1");  // Same password
        QByteArray apdu2 = channel.lastApdu;
        
        // Challenges should be different (random)
        QVERIFY(apdu1 != apdu2);
        
        qDebug() << "   ✅ Random challenges generated";
    }
    
    void testPairCryptogramVerification() {
        qDebug() << "Test: PAIR cryptogram verification";
        
        MockChannel channel;
        CommandSet cmdSet(&channel);
        
        // Mock SELECT
        channel.mockResponse = QByteArray::fromHex("A4") +
                               QByteArray::fromHex("17") +
                               QByteArray::fromHex("8F10") + QByteArray(16, 0xAA) +
                               QByteArray::fromHex("8040") + QByteArray(65, 0xBB) +
                               QByteArray::fromHex("9000");
        cmdSet.select();
        
        // Mock invalid cryptogram
        QByteArray wrongCryptogram(32, 0xFF);  // Wrong cryptogram
        QByteArray challenge(32, 0xDD);
        channel.mockResponse = wrongCryptogram + challenge + QByteArray::fromHex("9000");
        
        PairingInfo info = cmdSet.pair("password");
        
        // Should fail at cryptogram verification
        QVERIFY(!info.isValid());
        QVERIFY(cmdSet.lastError().contains("cryptogram"));
        
        qDebug() << "   Error:" << cmdSet.lastError();
        qDebug() << "   ✅ Detects invalid cryptogram";
    }
    
    void testPairShortResponse() {
        qDebug() << "Test: PAIR with short response";
        
        MockChannel channel;
        CommandSet cmdSet(&channel);
        
        // Mock SELECT
        channel.mockResponse = QByteArray::fromHex("A4") +
                               QByteArray::fromHex("17") +
                               QByteArray::fromHex("8F10") + QByteArray(16, 0xAA) +
                               QByteArray::fromHex("8040") + QByteArray(65, 0xBB) +
                               QByteArray::fromHex("9000");
        cmdSet.select();
        
        // Mock short response (less than 64 bytes)
        channel.mockResponse = QByteArray(30, 0xCC) + QByteArray::fromHex("9000");
        
        PairingInfo info = cmdSet.pair("password");
        
        // Should fail
        QVERIFY(!info.isValid());
        QVERIFY(cmdSet.lastError().contains("size"));
        
        qDebug() << "   ✅ Detects short response";
    }
    
    void testPairErrorResponse() {
        qDebug() << "Test: PAIR with error response";
        
        MockChannel channel;
        CommandSet cmdSet(&channel);
        
        // Mock SELECT
        channel.mockResponse = QByteArray::fromHex("A4") +
                               QByteArray::fromHex("17") +
                               QByteArray::fromHex("8F10") + QByteArray(16, 0xAA) +
                               QByteArray::fromHex("8040") + QByteArray(65, 0xBB) +
                               QByteArray::fromHex("9000");
        cmdSet.select();
        
        // Mock error response (wrong PIN or not initialized)
        channel.mockResponse = QByteArray::fromHex("6985");  // Conditions not satisfied
        
        PairingInfo info = cmdSet.pair("password");
        
        // Should fail
        QVERIFY(!info.isValid());
        
        qDebug() << "   ✅ Handles error response";
    }

    // ========== Secrets Tests ==========
    
    void testSecretsValidation() {
        qDebug() << "Test: Secrets validation";
        
        // Valid secrets
        Secrets s1("123456", "123456789012", "password");
        QCOMPARE(s1.pin, QString("123456"));
        QCOMPARE(s1.puk, QString("123456789012"));
        QCOMPARE(s1.pairingPassword, QString("password"));
        
        // Different secrets
        Secrets s2("000000", "999999999999", "different");
        QVERIFY(s1.pin != s2.pin);
        QVERIFY(s1.puk != s2.puk);
        
        qDebug() << "   ✅ Secrets stored correctly";
    }
    
    void testPairingInfoValidation() {
        qDebug() << "Test: PairingInfo validation";
        
        // Valid pairing info
        QByteArray key(32, 0xAA);
        PairingInfo p1(key, 1);
        QVERIFY(p1.isValid());
        QCOMPARE(p1.key, key);
        QCOMPARE(p1.index, (uint8_t)1);
        
        // Invalid pairing info (empty key)
        PairingInfo p2;
        QVERIFY(!p2.isValid());
        
        // Invalid pairing info (empty key, negative index)
        PairingInfo p3(QByteArray(), -1);
        QVERIFY(!p3.isValid());
        
        qDebug() << "   ✅ PairingInfo validation works";
    }

    void cleanupTestCase() {
        qDebug() << "INIT and PAIR tests complete";
    }
};

QTEST_MAIN(TestInitPair)
#include "test_init_pair.moc"

