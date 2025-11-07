#include "keycard-qt/command_set.h"
#include <QDebug>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QIODevice>
#include <QThread>

namespace Keycard {

// AID for Keycard applet (default instance = 1)
// Base AID: A0 00 00 08 04 00 01 01 (8 bytes)
// Instance: 01 (1 byte) - default instance index
static const QByteArray KEYCARD_DEFAULT_INSTANCE_AID =
    QByteArray::fromHex("A00000080400010101");

// Helper: PBKDF2-HMAC-SHA256 for pairing password derivation
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

CommandSet::CommandSet(IChannel* channel)
    : m_channel(channel)
    , m_secureChannel(new SecureChannel(channel))
    , m_remainingPINAttempts(-1)
{
    if (!m_channel) {
        qWarning() << "CommandSet: Null channel provided";
    }
}

CommandSet::~CommandSet() = default;

bool CommandSet::checkOK(const APDU::Response& response)
{
    if (!response.isOK()) {
        m_lastError = QString("APDU error: SW=%1").arg(response.sw(), 4, 16, QChar('0'));
        qWarning() << m_lastError;
        return false;
    }
    m_lastError.clear();
    return true;
}

APDU::Command CommandSet::buildCommand(uint8_t ins, uint8_t p1, uint8_t p2, const QByteArray& data)
{
    APDU::Command cmd(APDU::CLA, ins, p1, p2);
    if (!data.isEmpty()) {
        cmd.setData(data);
    }
    return cmd;
}

ApplicationInfo CommandSet::select()
{
    qDebug() << "📞 CommandSet::select() - START - Thread:" << QThread::currentThread();
    
    // Build SELECT command for Keycard applet
    APDU::Command cmd(APDU::CLA_ISO7816, APDU::INS_SELECT, 0x04, 0x00);
    cmd.setData(KEYCARD_DEFAULT_INSTANCE_AID);
    cmd.setLe(0);  // Expect response data
    
    // Send command
    QByteArray rawResponse = m_channel->transmit(cmd.serialize());
    APDU::Response response(rawResponse);
    
    if (!checkOK(response)) {
        return ApplicationInfo();
    }
    
    // Parse application info
    m_appInfo = parseApplicationInfo(response.data());
    
    // Generate ECDH secret if card supports secure channel
    if (!m_appInfo.secureChannelPublicKey.isEmpty()) {
        qDebug() << "CommandSet: Generating ECDH secret";
        m_secureChannel->generateSecret(m_appInfo.secureChannelPublicKey);
        qDebug() << "CommandSet: ECDH secret generated, keeping for secure channel open";
        // NOTE: We keep the secret for both initialized and uninitialized cards
        // - Initialized cards: Need it for OPEN_SECURE_CHANNEL
        // - Uninitialized cards: Need it for INIT
        // The secret will be cleared when we reset() the secure channel later
    }
    
    return m_appInfo;
}

PairingInfo CommandSet::pair(const QString& pairingPassword)
{
    qDebug() << "CommandSet: PAIR with password:" << pairingPassword;
    
    // Step 1: Send random challenge
    QByteArray challenge(32, 0);
    QRandomGenerator::global()->fillRange(
        reinterpret_cast<quint32*>(challenge.data()), 
        challenge.size() / sizeof(quint32)
    );
    
    qDebug() << "CommandSet: Challenge:" << challenge.toHex();
    
    APDU::Command cmd1 = buildCommand(APDU::INS_PAIR, APDU::P1PairFirstStep, 0, challenge);
    QByteArray rawResp1 = m_channel->transmit(cmd1.serialize());
    APDU::Response resp1(rawResp1);
    
    if (!checkOK(resp1)) {
        // Check for specific error: no available pairing slots
        if (resp1.sw() == 0x6A84) {
            m_lastError = "No available pairing slots (SW=6A84). "
                         "All pairing slots are full. To fix:\n"
                         "1. Use an existing pairing from your saved pairings file\n"
                         "2. Use Keycard Connect app to clear pairings\n"
                         "3. Factory reset the card (WARNING: erases all data)";
            qWarning() << "========================================";
            qWarning() << "❌ PAIRING FAILED: No available slots!";
            qWarning() << "❌ Your Keycard has all pairing slots full.";
            qWarning() << "========================================";
            qWarning() << "💡 Solutions:";
            qWarning() << "💡 1. Check if you have a saved pairing in your pairings file";
            qWarning() << "💡 2. Download Keycard Connect app and clear old pairings";
            qWarning() << "💡 3. Factory reset (WARNING: erases all keys!)";
            qWarning() << "========================================";
        } else {
            m_lastError = QString("Pair step 1 failed: %1").arg(resp1.errorMessage());
        }
        return PairingInfo();
    }
    
    if (resp1.data().size() < 64) {
        m_lastError = "Invalid pair response size";
        return PairingInfo();
    }
    
    QByteArray cardCryptogram = resp1.data().left(32);
    QByteArray cardChallenge = resp1.data().mid(32, 32);
    
    qDebug() << "CommandSet: Card cryptogram:" << cardCryptogram.toHex();
    qDebug() << "CommandSet: Card challenge:" << cardChallenge.toHex();
    
    // Step 2: Derive secret hash using PBKDF2
    QByteArray secretHash = derivePairingToken(pairingPassword);
    qDebug() << "CommandSet: Secret hash:" << secretHash.left(16).toHex() << "...";
    
    // Verify card cryptogram: expected = SHA256(secretHash + challenge)
    QCryptographicHash hashVerify(QCryptographicHash::Sha256);
    hashVerify.addData(secretHash);
    hashVerify.addData(challenge);
    QByteArray expectedCryptogram = hashVerify.result();
    
    if (expectedCryptogram != cardCryptogram) {
        m_lastError = "Invalid card cryptogram - wrong pairing password";
        qWarning() << "========================================";
        qWarning() << "❌ CommandSet: CRYPTOGRAM MISMATCH!";
        qWarning() << "❌ This means the pairing password is WRONG!";
        qWarning() << "========================================";
        qWarning() << "Password used:" << pairingPassword;
        qWarning() << "Expected cryptogram:" << expectedCryptogram.toHex();
        qWarning() << "Received cryptogram:" << cardCryptogram.toHex();
        qWarning() << "========================================";
        qWarning() << "💡 TIP: Card may need to be initialized first with KeycardInitialize.init()";
        qWarning() << "💡 OR: Card was initialized with a different pairing password";
        qWarning() << "========================================";
        return PairingInfo();
    }
    
    qDebug() << "CommandSet: Card cryptogram verified!";
    
    // Compute our response: SHA256(secretHash + cardChallenge)
    QCryptographicHash hash2(QCryptographicHash::Sha256);
    hash2.addData(secretHash);
    hash2.addData(cardChallenge);
    QByteArray ourCryptogram = hash2.result();
    
    qDebug() << "CommandSet: Our cryptogram:" << ourCryptogram.toHex();
    
    APDU::Command cmd2 = buildCommand(APDU::INS_PAIR, APDU::P1PairFinalStep, 0, ourCryptogram);
    QByteArray rawResp2 = m_channel->transmit(cmd2.serialize());
    APDU::Response resp2(rawResp2);
    
    if (!checkOK(resp2)) {
        m_lastError = "Pair step 2 failed";
        return PairingInfo();
    }
    
    if (resp2.data().isEmpty()) {
        m_lastError = "No pairing data in response";
        return PairingInfo();
    }
    
    // Parse pairing info
    uint8_t pairingIndex = static_cast<uint8_t>(resp2.data()[0]);
    QByteArray salt = resp2.data().mid(1);
    
    // Compute pairing key: SHA256(secretHash + salt)
    QCryptographicHash hash3(QCryptographicHash::Sha256);
    hash3.addData(secretHash);
    hash3.addData(salt);
    QByteArray pairingKey = hash3.result();
    
    m_pairingInfo = PairingInfo(pairingKey, pairingIndex);
    
    qDebug() << "CommandSet: Paired! Index:" << pairingIndex << "Key:" << pairingKey.toHex();
    
    return m_pairingInfo;
}

bool CommandSet::openSecureChannel(const PairingInfo& pairingInfo)
{
    qDebug() << "📞 CommandSet::openSecureChannel() - START - Thread:" << QThread::currentThread();
    qDebug() << "📞   Pairing index:" << pairingInfo.index << "Key:" << pairingInfo.key.toHex();
    
    if (!pairingInfo.isValid()) {
        m_lastError = "Invalid pairing info";
        return false;
    }
    
    m_pairingInfo = pairingInfo;
    
    // Build OPEN_SECURE_CHANNEL command
    // P1 = pairing index, data = our ephemeral public key
    QByteArray data = m_secureChannel->rawPublicKey();
    
    if (data.isEmpty()) {
        m_lastError = "No public key available - secure channel not initialized";
        return false;
    }
    
    APDU::Command cmd = buildCommand(APDU::INS_OPEN_SECURE_CHANNEL, pairingInfo.index, 0, data);
    QByteArray rawResp = m_channel->transmit(cmd.serialize());
    APDU::Response resp(rawResp);
    
    if (!checkOK(resp)) {
        m_lastError = "Failed to open secure channel";
        return false;
    }
    
    // Derive session keys from response
    // cardData format: [salt (32 bytes)][iv (16 bytes)]
    QByteArray cardData = resp.data();
    
    if (cardData.size() < 48) {
        m_lastError = "Invalid card data size for session key derivation";
        return false;
    }
    
    QByteArray salt = cardData.left(32);
    QByteArray iv = cardData.mid(32);
    
    // Derive encryption and MAC keys using SHA-512 (matching Go's DeriveSessionKeys)
    // hash = SHA512(secret + pairing_key + salt)
    // enc_key = hash[0:32]  (first 32 bytes)
    // mac_key = hash[32:64] (last 32 bytes)
    
    QCryptographicHash hash(QCryptographicHash::Sha512);
    hash.addData(m_secureChannel->secret());
    hash.addData(pairingInfo.key);
    hash.addData(salt);
    QByteArray result = hash.result();  // 64 bytes
    
    QByteArray encKey = result.left(32);   // First 32 bytes for AES-256
    QByteArray macKey = result.mid(32);    // Last 32 bytes for MAC
    
    // Initialize secure channel
    m_secureChannel->init(iv, encKey, macKey);
    
    // Perform mutual authentication
    if (!mutualAuthenticate()) {
        m_lastError = "Mutual authentication failed";
        return false;
    }

    qDebug() << "CommandSet: Secure channel opened with mutual auth!";
    qDebug() << "📞 CommandSet::openSecureChannel() - END - Success";
    return true;
}

bool CommandSet::mutualAuthenticate()
{
    qDebug() << "CommandSet: Performing mutual authentication";
    
    // Generate random 32-byte challenge
    QByteArray challenge(32, 0);
    QRandomGenerator::global()->fillRange(
        reinterpret_cast<quint32*>(challenge.data()),
        challenge.size() / sizeof(quint32)
    );
    
    qDebug() << "CommandSet: Challenge:" << challenge.toHex();
    
    APDU::Command cmd = buildCommand(0x11, 0, 0, challenge);  // INS_MUTUALLY_AUTHENTICATE
    APDU::Response resp = m_secureChannel->send(cmd);
    
    if (!checkOK(resp)) {
        return false;
    }
    
    qDebug() << "CommandSet: Mutual authentication successful";
    return true;
}

bool CommandSet::init(const Secrets& secrets)
{
    qDebug() << "CommandSet: INIT";
    
    // Validate secrets
    if (secrets.pin.length() != 6) {
        m_lastError = "PIN must be 6 digits";
        qWarning() << m_lastError;
        return false;
    }
    
    if (secrets.puk.length() != 12) {
        m_lastError = "PUK must be 12 digits";
        qWarning() << m_lastError;
        return false;
    }
    
    if (secrets.pairingPassword.length() < 5) {
        m_lastError = "Pairing password must be at least 5 characters";
        qWarning() << m_lastError;
        return false;
    }
    
    // Build plaintext data: PIN + PUK + PairingToken
    // PairingToken = PBKDF2(password, salt, 50000 iterations, 32 bytes)
    QByteArray plainData;
    plainData.append(secrets.pin.toUtf8());
    plainData.append(secrets.puk.toUtf8());
    
    // Derive pairing token using PBKDF2-HMAC-SHA256
    QByteArray pairingToken = derivePairingToken(secrets.pairingPassword);
    plainData.append(pairingToken);
    
    qDebug() << "CommandSet: Pairing token derived:" << pairingToken.left(16).toHex() << "...";
    
    // Encrypt with one-shot encryption (using shared secret from SELECT)
    QByteArray encryptedData = m_secureChannel->oneShotEncrypt(plainData);
    
    if (encryptedData.isEmpty()) {
        m_lastError = "Failed to encrypt INIT data";
        return false;
    }
    
    // Send INIT command with encrypted data
    APDU::Command cmd = buildCommand(APDU::INS_INIT, 0, 0, encryptedData);
    QByteArray rawResp = m_channel->transmit(cmd.serialize());
    APDU::Response resp(rawResp);
    
    if (!checkOK(resp)) {
        return false;
    }
    
    qDebug() << "CommandSet: Card initialized successfully";
    
    // After init, we need to SELECT again to get initialized state
    m_appInfo = select();
    
    return true;
}

bool CommandSet::unpair(uint8_t index)
{
    qDebug() << "CommandSet: UNPAIR index:" << index;
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return false;
    }
    
    APDU::Command cmd = buildCommand(APDU::INS_UNPAIR, index);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    return checkOK(resp);
}

ApplicationStatus CommandSet::getStatus(uint8_t info)
{
    qDebug() << "📞 CommandSet::getStatus() - START - Thread:" << QThread::currentThread();
    qDebug() << "📞   Info parameter:" << info;
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return ApplicationStatus();
    }
    
    APDU::Command cmd = buildCommand(APDU::INS_GET_STATUS, info);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    if (!checkOK(resp)) {
        return ApplicationStatus();
    }
    
    return parseApplicationStatus(resp.data());
}

bool CommandSet::verifyPIN(const QString& pin)
{
    qDebug() << "📞 CommandSet::verifyPIN() - START - Thread:" << QThread::currentThread();
    qDebug() << "📞   PIN length:" << pin.length();
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return false;
    }
    
    qDebug() << "CommandSet: Building VERIFY_PIN command";
    APDU::Command cmd = buildCommand(APDU::INS_VERIFY_PIN, 0, 0, pin.toUtf8());
    qDebug() << "📞 CommandSet: Sending encrypted VERIFY_PIN command via SecureChannel";
    APDU::Response resp = m_secureChannel->send(cmd);
    qDebug() << "CommandSet: Received response SW:" << QString("0x%1").arg(resp.sw(), 4, 16, QChar('0'));
    
    // CRITICAL FIX for hot-plug PIN validation:
    // On hot-plugged cards, the first VERIFY_PIN may fail with 0x6f05 (invalid MAC)
    // even though the secure channel is correctly established. This appears to be
    // a card internal state issue where the crypto engine needs one command to
    // fully synchronize. Automatic retry resolves this transparently to the user.
    if (resp.sw() == 0x6f05) {
        qWarning() << "CommandSet: VERIFY_PIN failed with 0x6f05, retrying once (hot-plug state sync issue)";
        QThread::msleep(50);  // Brief delay before retry
        resp = m_secureChannel->send(cmd);
        qDebug() << "CommandSet: Retry response SW:" << QString("0x%1").arg(resp.sw(), 4, 16, QChar('0'));
    }
    
    // Check for wrong PIN (SW1=0x63, SW2=0xCX where X = remaining attempts)
    if ((resp.sw() & 0x63C0) == 0x63C0) {
        m_remainingPINAttempts = resp.sw() & 0x000F;
        m_lastError = QString("Wrong PIN. Remaining attempts: %1").arg(m_remainingPINAttempts);
        qWarning() << m_lastError;
        qDebug() << "📞 CommandSet::verifyPIN() - END - FAILED (Wrong PIN)";
        return false;
    }
    
    m_remainingPINAttempts = -1;
    bool result = checkOK(resp);
    if (result) {
        qDebug() << "📞 CommandSet::verifyPIN() - END - Success (PIN correct)";
    } else {
        qDebug() << "📞 CommandSet::verifyPIN() - END - FAILED with SW:" << QString("0x%1").arg(resp.sw(), 4, 16, QChar('0'));
    }
    return result;
}

// Helper function to parse BIP32 derivation path
static QByteArray parseDerivationPath(const QString& path, uint8_t& startingPoint)
{
    // Parse path like "m/44'/60'/0'/0/0" or "../0/0"
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    QString cleanPath = path.trimmed();
    
    if (cleanPath.startsWith("m/")) {
        startingPoint = APDU::P1DeriveKeyFromMaster;
        cleanPath = cleanPath.mid(2);  // Remove "m/"
    } else if (cleanPath.startsWith("../")) {
        startingPoint = APDU::P1DeriveKeyFromParent;
        cleanPath = cleanPath.mid(3);  // Remove "../"
    } else if (cleanPath.startsWith("./")) {
        startingPoint = APDU::P1DeriveKeyFromCurrent;
        cleanPath = cleanPath.mid(2);  // Remove "./"
    } else {
        startingPoint = APDU::P1DeriveKeyFromCurrent;
    }
    
    if (cleanPath.isEmpty()) {
        return result;
    }
    
    QStringList segments = cleanPath.split('/');
    for (const QString& segment : segments) {
        bool ok;
        uint32_t value = segment.endsWith("'") || segment.endsWith("h")
            ? segment.left(segment.length() - 1).toUInt(&ok) | 0x80000000
            : segment.toUInt(&ok);
        
        if (ok) {
            stream << value;
        }
    }
    
    return result;
}

// Security operations

bool CommandSet::changePIN(const QString& newPIN)
{
    qDebug() << "CommandSet: CHANGE_PIN";
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return false;
    }
    
    APDU::Command cmd = buildCommand(APDU::INS_CHANGE_PIN, APDU::P1ChangePinPIN, 0, newPIN.toUtf8());
    APDU::Response resp = m_secureChannel->send(cmd);
    
    return checkOK(resp);
}

bool CommandSet::changePUK(const QString& newPUK)
{
    qDebug() << "CommandSet: CHANGE_PUK";
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return false;
    }
    
    APDU::Command cmd = buildCommand(APDU::INS_CHANGE_PIN, APDU::P1ChangePinPUK, 0, newPUK.toUtf8());
    APDU::Response resp = m_secureChannel->send(cmd);
    
    return checkOK(resp);
}

bool CommandSet::unblockPIN(const QString& puk, const QString& newPIN)
{
    qDebug() << "CommandSet: UNBLOCK_PIN";
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return false;
    }
    
    // Concatenate PUK and new PIN
    QByteArray data = puk.toUtf8() + newPIN.toUtf8();
    APDU::Command cmd = buildCommand(APDU::INS_UNBLOCK_PIN, 0, 0, data);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    // Check for wrong PUK (SW1=0x63, SW2=0xCX where X = remaining attempts)
    if ((resp.sw() & 0x63C0) == 0x63C0) {
        int remaining = resp.sw() & 0x000F;
        m_lastError = QString("Wrong PUK. Remaining attempts: %1").arg(remaining);
        qWarning() << m_lastError;
        return false;
    }
    
    return checkOK(resp);
}

bool CommandSet::changePairingSecret(const QString& newPassword)
{
    qDebug() << "CommandSet: CHANGE_PAIRING_SECRET";
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return false;
    }
    
    QByteArray data = newPassword.toUtf8();
    APDU::Command cmd = buildCommand(APDU::INS_CHANGE_PIN, APDU::P1ChangePinPairingSecret, 0, data);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    return checkOK(resp);
}

// Key management

QByteArray CommandSet::generateKey()
{
    qDebug() << "CommandSet: GENERATE_KEY";
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return QByteArray();
    }
    
    APDU::Command cmd = buildCommand(APDU::INS_GENERATE_KEY, 0, 0);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    if (!checkOK(resp)) {
        return QByteArray();
    }
    
    // Response is the key UID (32 bytes)
    return resp.data();
}

QVector<int> CommandSet::generateMnemonic(int checksumSize)
{
    qDebug() << "CommandSet: GENERATE_MNEMONIC checksumSize:" << checksumSize;
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return QVector<int>();
    }
    
    APDU::Command cmd = buildCommand(APDU::INS_GENERATE_MNEMONIC, static_cast<uint8_t>(checksumSize), 0);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    if (!checkOK(resp)) {
        return QVector<int>();
    }
    
    // Parse mnemonic indexes (2 bytes per index, big endian)
    QVector<int> indexes;
    QByteArray data = resp.data();
    for (int i = 0; i + 1 < data.size(); i += 2) {
        uint16_t index = (static_cast<uint8_t>(data[i]) << 8) | static_cast<uint8_t>(data[i + 1]);
        indexes.append(index);
    }
    
    return indexes;
}

QByteArray CommandSet::loadSeed(const QByteArray& seed)
{
    qDebug() << "CommandSet: LOAD_SEED";
    
    // Validate input first
    if (seed.size() != 64) {
        m_lastError = "Seed must be 64 bytes";
        qWarning() << m_lastError;
        return QByteArray();
    }
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return QByteArray();
    }
    
    APDU::Command cmd = buildCommand(APDU::INS_LOAD_KEY, APDU::P1LoadKeySeed, 0, seed);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    if (!checkOK(resp)) {
        return QByteArray();
    }
    
    // Response is the key UID (32 bytes)
    return resp.data();
}

bool CommandSet::removeKey()
{
    qDebug() << "CommandSet: REMOVE_KEY";
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return false;
    }
    
    APDU::Command cmd = buildCommand(APDU::INS_REMOVE_KEY, 0, 0);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    return checkOK(resp);
}

bool CommandSet::deriveKey(const QString& path)
{
    qDebug() << "CommandSet: DERIVE_KEY path:" << path;
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return false;
    }
    
    uint8_t startingPoint = APDU::P1DeriveKeyFromMaster;
    QByteArray pathData = parseDerivationPath(path, startingPoint);
    
    APDU::Command cmd = buildCommand(APDU::INS_DERIVE_KEY, startingPoint, 0, pathData);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    return checkOK(resp);
}

// Signing

QByteArray CommandSet::sign(const QByteArray& data)
{
    qDebug() << "CommandSet: SIGN";
    
    // Validate input first
    if (data.size() != 32) {
        m_lastError = "Data must be 32 bytes (hash)";
        qWarning() << m_lastError;
        return QByteArray();
    }
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return QByteArray();
    }
    
    APDU::Command cmd = buildCommand(APDU::INS_SIGN, APDU::P1SignCurrentKey, 1, data);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    if (!checkOK(resp)) {
        return QByteArray();
    }
    
    // Response is signature: pubkey (65 bytes) + signature (rest)
    // We return just the signature part (typically 65 bytes: R + S + V)
    QByteArray fullResp = resp.data();
    if (fullResp.size() > 65) {
        return fullResp.mid(65);  // Skip public key, return signature
    }
    return fullResp;
}

QByteArray CommandSet::signWithPath(const QByteArray& data, const QString& path, bool makeCurrent)
{
    qDebug() << "CommandSet: SIGN_WITH_PATH path:" << path << "makeCurrent:" << makeCurrent;
    
    // Validate input first
    if (data.size() != 32) {
        m_lastError = "Data must be 32 bytes (hash)";
        qWarning() << m_lastError;
        return QByteArray();
    }
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return QByteArray();
    }
    
    uint8_t startingPoint = APDU::P1DeriveKeyFromMaster;
    QByteArray pathData = parseDerivationPath(path, startingPoint);
    
    uint8_t p1 = makeCurrent ? APDU::P1SignDeriveAndMakeCurrent : APDU::P1SignDerive;
    
    // Concatenate data + path
    QByteArray cmdData = data + pathData;
    
    APDU::Command cmd = buildCommand(APDU::INS_SIGN, p1, 1, cmdData);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    if (!checkOK(resp)) {
        return QByteArray();
    }
    
    // Skip public key, return signature
    QByteArray fullResp = resp.data();
    if (fullResp.size() > 65) {
        return fullResp.mid(65);
    }
    return fullResp;
}

QByteArray CommandSet::signWithPathFullResponse(const QByteArray& data, const QString& path, bool makeCurrent)
{
    qDebug() << "CommandSet: SIGN_WITH_PATH_FULL_RESPONSE path:" << path << "makeCurrent:" << makeCurrent;
    
    // Validate input first
    if (data.size() != 32) {
        m_lastError = "Data must be 32 bytes (hash)";
        qWarning() << m_lastError;
        return QByteArray();
    }
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return QByteArray();
    }
    
    uint8_t startingPoint = APDU::P1DeriveKeyFromMaster;
    QByteArray pathData = parseDerivationPath(path, startingPoint);
    
    uint8_t p1 = makeCurrent ? APDU::P1SignDeriveAndMakeCurrent : APDU::P1SignDerive;
    
    // Concatenate data + path
    QByteArray cmdData = data + pathData;
    
    APDU::Command cmd = buildCommand(APDU::INS_SIGN, p1, 1, cmdData);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    if (!checkOK(resp)) {
        return QByteArray();
    }
    
    // Return the full TLV response (includes public key and signature)
    return resp.data();
}

QByteArray CommandSet::signPinless(const QByteArray& data)
{
    qDebug() << "CommandSet: SIGN_PINLESS";
    
    // Validate input first
    if (data.size() != 32) {
        m_lastError = "Data must be 32 bytes (hash)";
        qWarning() << m_lastError;
        return QByteArray();
    }
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return QByteArray();
    }
    
    APDU::Command cmd = buildCommand(APDU::INS_SIGN, APDU::P1SignPinless, 1, data);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    if (!checkOK(resp)) {
        return QByteArray();
    }
    
    // Skip public key, return signature
    QByteArray fullResp = resp.data();
    if (fullResp.size() > 65) {
        return fullResp.mid(65);
    }
    return fullResp;
}

bool CommandSet::setPinlessPath(const QString& path)
{
    qDebug() << "CommandSet: SET_PINLESS_PATH path:" << path;
    
    // Validate input first
    if (!path.startsWith("m/")) {
        m_lastError = "Pinless path must be absolute (start with m/)";
        qWarning() << m_lastError;
        return false;
    }
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return false;
    }
    
    uint8_t startingPoint = APDU::P1DeriveKeyFromMaster;
    QByteArray pathData = parseDerivationPath(path, startingPoint);
    
    APDU::Command cmd = buildCommand(APDU::INS_SET_PINLESS_PATH, 0, 0, pathData);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    return checkOK(resp);
}

// Data storage

bool CommandSet::storeData(uint8_t type, const QByteArray& data)
{
    qDebug() << "CommandSet: STORE_DATA type:" << type << "size:" << data.size();
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return false;
    }
    
    APDU::Command cmd = buildCommand(APDU::INS_STORE_DATA, type, 0, data);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    return checkOK(resp);
}

QByteArray CommandSet::getData(uint8_t type)
{
    qDebug() << "CommandSet: GET_DATA type:" << type;
    
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return QByteArray();
    }
    
    APDU::Command cmd = buildCommand(APDU::INS_GET_DATA, type, 0);
    APDU::Response resp = m_secureChannel->send(cmd);
    
    if (!checkOK(resp)) {
        return QByteArray();
    }
    
    return resp.data();
}

// Utilities

QByteArray CommandSet::identify(const QByteArray& challenge)
{
    qDebug() << "CommandSet: IDENTIFY";
    
    QByteArray challengeData = challenge;
    if (challengeData.isEmpty()) {
        // Generate random 32-byte challenge
        challengeData.resize(32);
        QRandomGenerator::global()->fillRange(
            reinterpret_cast<quint32*>(challengeData.data()), 
            challengeData.size() / sizeof(quint32)
        );
    }
    
    // IDENTIFY uses standard CLA (0x00), not secure channel CLA (0x80)
    APDU::Command cmd(APDU::CLA_ISO7816, APDU::INS_IDENTIFY, 0, 0);
    cmd.setData(challengeData);
    QByteArray rawResp = m_channel->transmit(cmd.serialize());
    APDU::Response resp(rawResp);
    
    if (!checkOK(resp)) {
        return QByteArray();
    }
    
    return resp.data();
}

QByteArray CommandSet::exportKey(bool derive, bool makeCurrent, const QString& path, uint8_t exportType)
{
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return QByteArray();
    }

    uint8_t p1 = APDU::P1ExportKeyCurrent;
    QByteArray pathData;

    if (derive) {
        uint8_t startingPoint = APDU::P1DeriveKeyFromMaster;
        pathData = parseDerivationPath(path, startingPoint);
        p1 = makeCurrent ? APDU::P1ExportKeyDeriveAndMakeCurrent : APDU::P1ExportKeyDerive;
        p1 |= startingPoint;
    }

    APDU::Command cmd = buildCommand(APDU::INS_EXPORT_KEY, p1, exportType, pathData);
    cmd.setLe(0xFF); // Request up to 255 bytes
    
    APDU::Response resp = m_secureChannel->send(cmd);
    
    if (!checkOK(resp)) {
        m_lastError = QString("EXPORT_KEY failed with SW: 0x%1").arg(resp.sw(), 4, 16, QChar('0'));
        return QByteArray();
    }

    return resp.data();
}

QByteArray CommandSet::exportKeyExtended(bool derive, bool makeCurrent, const QString& path, uint8_t exportType)
{
    if (!m_secureChannel->isOpen()) {
        m_lastError = "Secure channel not open";
        return QByteArray();
    }
    
    uint8_t p1 = APDU::P1ExportKeyCurrent;
    QByteArray pathData;
    
    if (derive) {
        uint8_t startingPoint = APDU::P1DeriveKeyFromMaster;
        pathData = parseDerivationPath(path, startingPoint);
        p1 = makeCurrent ? APDU::P1ExportKeyDeriveAndMakeCurrent : APDU::P1ExportKeyDerive;
        p1 |= startingPoint;
    }
    
    APDU::Command cmd = buildCommand(APDU::INS_EXPORT_KEY, p1, exportType, pathData);
    cmd.setLe(0);  // Request all available response data
    
    APDU::Response resp = m_secureChannel->send(cmd);
    
    if (!checkOK(resp)) {
        m_lastError = QString("EXPORT_KEY_EXTENDED failed with SW: 0x%1").arg(resp.sw(), 4, 16, QChar('0'));
        return QByteArray();
    }
    
    return resp.data();
}

bool CommandSet::factoryReset()
{
    qDebug() << "CommandSet: FACTORY_RESET";
    
    // CRITICAL: Select the Keycard applet first (matches keycard-go implementation)
    // The factory reset command requires the applet to be selected
    ApplicationInfo appInfo = select();
    if (!appInfo.initialized) {
        // Card already in factory state - nothing to do
        qDebug() << "CommandSet: Card already in factory state";
        return true;
    }
    
    // Send factory reset command
    APDU::Command cmd = buildCommand(APDU::INS_FACTORY_RESET, APDU::P1FactoryResetMagic, APDU::P2FactoryResetMagic);
    QByteArray rawResp = m_channel->transmit(cmd.serialize());
    APDU::Response resp(rawResp);
    
    if (checkOK(resp)) {
        // Reset secure channel after factory reset
        m_secureChannel->reset();
        m_appInfo = ApplicationInfo();
        m_pairingInfo = PairingInfo();
        return true;
    }
    
    return false;
}

} // namespace Keycard

