/**
 * Unit tests for PBKDF2-HMAC-SHA256 implementation
 * 
 * Tests the pairing password derivation used for INIT and PAIR commands
 */

#include <QTest>
#include <QDebug>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>

// We need to test the internal PBKDF2 function
// Since it's static in command_set.cpp, we'll reimplement it here for testing
static QByteArray derivePairingToken(const QString& password)
{
    QByteArray salt = "Keycard Pairing Password Salt";
    QByteArray passwordBytes = password.toUtf8();
    int iterations = 50000;
    int keyLength = 32;
    
    QByteArray result(keyLength, 0);
    QByteArray U, T;
    
    // PBKDF2: derived_key = PBKDF2(password, salt, iterations, keyLength)
    // Using HMAC-SHA256
    for (int block = 1; block <= (keyLength + 31) / 32; ++block) {
        // U_1 = HMAC(password, salt || INT(block))
        QByteArray blockData = salt;
        blockData.append((char)((block >> 24) & 0xFF));
        blockData.append((char)((block >> 16) & 0xFF));
        blockData.append((char)((block >> 8) & 0xFF));
        blockData.append((char)(block & 0xFF));
        
        U = QMessageAuthenticationCode::hash(blockData, passwordBytes, QCryptographicHash::Sha256);
        T = U;
        
        // U_2 through U_iterations
        for (int i = 1; i < iterations; ++i) {
            U = QMessageAuthenticationCode::hash(U, passwordBytes, QCryptographicHash::Sha256);
            // T = U_1 XOR U_2 XOR ... XOR U_iterations
            for (int j = 0; j < U.size(); ++j) {
                T[j] = T[j] ^ U[j];
            }
        }
        
        // Copy T to result
        int offset = (block - 1) * 32;
        int copyLen = qMin(32, keyLength - offset);
        for (int i = 0; i < copyLen; ++i) {
            result[offset + i] = T[i];
        }
    }
    
    return result;
}

class TestPBKDF2 : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qDebug() << "Testing PBKDF2-HMAC-SHA256 implementation";
    }

    void testBasicDerivation() {
        qDebug() << "Test: Basic PBKDF2 derivation";
        
        QString password = "KeycardTest";
        QByteArray token = derivePairingToken(password);
        
        // Should return 32 bytes
        QCOMPARE(token.size(), 32);
        
        // Should be deterministic (same password -> same token)
        QByteArray token2 = derivePairingToken(password);
        QCOMPARE(token, token2);
        
        qDebug() << "   Token:" << token.toHex();
        qDebug() << "   ✅ Deterministic 32-byte output";
    }
    
    void testDifferentPasswords() {
        qDebug() << "Test: Different passwords produce different tokens";
        
        QByteArray token1 = derivePairingToken("password1");
        QByteArray token2 = derivePairingToken("password2");
        QByteArray token3 = derivePairingToken("password3");
        
        // All should be different
        QVERIFY(token1 != token2);
        QVERIFY(token1 != token3);
        QVERIFY(token2 != token3);
        
        qDebug() << "   ✅ Different passwords produce unique tokens";
    }
    
    void testKnownVector() {
        qDebug() << "Test: Known test vector";
        
        // This is the password used in our integration tests
        QString password = "KeycardTest";
        QByteArray token = derivePairingToken(password);
        
        // Expected value (from successful INIT test)
        QByteArray expected = QByteArray::fromHex("05c6ce68c78760fd529232a37484d942");
        
        // Check first 16 bytes match
        QCOMPARE(token.left(16), expected);
        
        qDebug() << "   Expected:" << expected.toHex();
        qDebug() << "   Got:     " << token.left(16).toHex();
        qDebug() << "   ✅ Matches known vector";
    }
    
    void testEmptyPassword() {
        qDebug() << "Test: Empty password";
        
        QByteArray token = derivePairingToken("");
        
        // Should still work (32 bytes)
        QCOMPARE(token.size(), 32);
        
        // Should be different from non-empty
        QByteArray token2 = derivePairingToken("a");
        QVERIFY(token != token2);
        
        qDebug() << "   ✅ Handles empty password";
    }
    
    void testLongPassword() {
        qDebug() << "Test: Long password";
        
        QString longPass = QString("a").repeated(1000);
        QByteArray token = derivePairingToken(longPass);
        
        // Should still work
        QCOMPARE(token.size(), 32);
        
        qDebug() << "   ✅ Handles 1000-character password";
    }
    
    void testSpecialCharacters() {
        qDebug() << "Test: Special characters in password";
        
        QByteArray token1 = derivePairingToken("password!");
        QByteArray token2 = derivePairingToken("password@");
        QByteArray token3 = derivePairingToken("pässwörd");
        
        // All should be valid and different
        QCOMPARE(token1.size(), 32);
        QCOMPARE(token2.size(), 32);
        QCOMPARE(token3.size(), 32);
        QVERIFY(token1 != token2);
        QVERIFY(token1 != token3);
        
        qDebug() << "   ✅ Handles special characters and UTF-8";
    }
    
    void testCaseSensitivity() {
        qDebug() << "Test: Password case sensitivity";
        
        QByteArray token1 = derivePairingToken("KeycardTest");
        QByteArray token2 = derivePairingToken("keycardtest");
        QByteArray token3 = derivePairingToken("KEYCARDTEST");
        
        // Should all be different (case-sensitive)
        QVERIFY(token1 != token2);
        QVERIFY(token1 != token3);
        QVERIFY(token2 != token3);
        
        qDebug() << "   ✅ Case-sensitive derivation";
    }
    
    void testPerformance() {
        qDebug() << "Test: Performance (50,000 iterations)";
        
        QElapsedTimer timer;
        timer.start();
        
        QByteArray token = derivePairingToken("TestPassword");
        
        qint64 elapsed = timer.elapsed();
        qDebug() << "   Time:" << elapsed << "ms";
        
        // Should take reasonable time (less than 5 seconds)
        QVERIFY(elapsed < 5000);
        
        // Verify result is valid
        QCOMPARE(token.size(), 32);
        
        qDebug() << "   ✅ Completed in" << elapsed << "ms";
    }
    
    void testHexEncoding() {
        qDebug() << "Test: Hex encoding of output";
        
        QByteArray token = derivePairingToken("test");
        QString hex = token.toHex();
        
        // Should be 64 hex characters (32 bytes)
        QCOMPARE(hex.length(), 64);
        
        // Should only contain hex characters
        QRegularExpression hexRegex("^[0-9a-f]{64}$");
        QVERIFY(hexRegex.match(hex).hasMatch());
        
        qDebug() << "   Hex:" << hex;
        qDebug() << "   ✅ Valid hex encoding";
    }
    
    void cleanupTestCase() {
        qDebug() << "PBKDF2 tests complete";
    }
};

QTEST_MAIN(TestPBKDF2)
#include "test_pbkdf2.moc"

