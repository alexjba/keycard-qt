#include "keycard-qt/secure_channel.h"
#include "keycard-qt/apdu/utils.h"
#include <QDebug>
#include <QCryptographicHash>
#include <QThread>
#include <stdexcept>

// OpenSSL is the sole cryptographic backend
#ifdef KEYCARD_QT_HAS_OPENSSL
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/obj_mac.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace Keycard {

// Private implementation (Pimpl idiom)
struct SecureChannel::Private {
    IChannel* channel = nullptr;
    
    // ECDH (OpenSSL)
#ifdef KEYCARD_QT_HAS_OPENSSL
    EVP_PKEY* privateKey = nullptr;
    EVP_PKEY* cardPublicKey = nullptr;
#endif
    QByteArray secret;
    QByteArray rawPublicKeyData;
    
    // Session keys
    QByteArray iv;
    QByteArray encKey;
    QByteArray macKey;
    bool open = false;
    
    // MAC state
    int openedIndex = -1;
    
    ~Private() {
#ifdef KEYCARD_QT_HAS_OPENSSL
        if (privateKey) EVP_PKEY_free(privateKey);
        if (cardPublicKey) EVP_PKEY_free(cardPublicKey);
#endif
    }
};

SecureChannel::SecureChannel(IChannel* channel)
    : d(new Private)
{
    d->channel = channel;
    
#ifndef KEYCARD_QT_HAS_OPENSSL
    qWarning() << "SecureChannel: Built without OpenSSL - secure channel not available";
#endif
}

SecureChannel::~SecureChannel() = default;

bool SecureChannel::generateSecret(const QByteArray& cardPublicKey)
{
    qDebug() << "SecureChannel: Generating ECDH secret (secp256k1)";
    qDebug() << "SecureChannel: Card public key:" << cardPublicKey.toHex();
    
#ifndef KEYCARD_QT_HAS_OPENSSL
    qWarning() << "SecureChannel: OpenSSL not available, cannot generate ECDH secret";
    return false;
#else
    // Validate card public key size (65 bytes: 0x04 + X + Y)
    if (cardPublicKey.size() != 65 || static_cast<uint8_t>(cardPublicKey[0]) != 0x04) {
        qWarning() << "SecureChannel: Invalid card public key format (expected 65 bytes starting with 0x04)";
        return false;
    }
    
    // Step 1: Generate our ephemeral EC key pair (secp256k1)
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!pctx) {
        qWarning() << "SecureChannel: Failed to create EVP_PKEY_CTX";
        return false;
    }
    
    if (EVP_PKEY_keygen_init(pctx) <= 0) {
        qWarning() << "SecureChannel: Failed to init keygen";
        EVP_PKEY_CTX_free(pctx);
        return false;
    }
    
    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_secp256k1) <= 0) {
        qWarning() << "SecureChannel: Failed to set secp256k1 curve";
        EVP_PKEY_CTX_free(pctx);
        return false;
    }
    
    if (EVP_PKEY_keygen(pctx, &d->privateKey) <= 0) {
        qWarning() << "SecureChannel: Failed to generate key pair";
        EVP_PKEY_CTX_free(pctx);
        return false;
    }
    
    EVP_PKEY_CTX_free(pctx);
    
    // Step 2: Extract our public key in uncompressed format
    EC_KEY* eckey = EVP_PKEY_get1_EC_KEY(d->privateKey);
    if (!eckey) {
        qWarning() << "SecureChannel: Failed to get EC_KEY";
        return false;
    }
    
    const EC_POINT* pubkey_point = EC_KEY_get0_public_key(eckey);
    const EC_GROUP* group = EC_KEY_get0_group(eckey);
    
    // Convert public key to uncompressed format (0x04 + X + Y)
    size_t pubkey_len = EC_POINT_point2oct(group, pubkey_point, 
                                           POINT_CONVERSION_UNCOMPRESSED,
                                           nullptr, 0, nullptr);
    
    d->rawPublicKeyData.resize(static_cast<int>(pubkey_len));
    EC_POINT_point2oct(group, pubkey_point, 
                      POINT_CONVERSION_UNCOMPRESSED,
                      reinterpret_cast<unsigned char*>(d->rawPublicKeyData.data()),
                      pubkey_len, nullptr);
    
    EC_KEY_free(eckey);
    
    qDebug() << "SecureChannel: Our public key:" << d->rawPublicKeyData.toHex();
    
    // Step 3: Import card's public key
    EC_KEY* card_eckey = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!card_eckey) {
        qWarning() << "SecureChannel: Failed to create EC_KEY for card";
        return false;
    }
    
    const EC_GROUP* card_group = EC_KEY_get0_group(card_eckey);
    EC_POINT* card_point = EC_POINT_new(card_group);
    
    if (EC_POINT_oct2point(card_group, card_point,
                          reinterpret_cast<const unsigned char*>(cardPublicKey.constData()),
                          cardPublicKey.size(), nullptr) != 1) {
        qWarning() << "SecureChannel: Failed to parse card public key";
        EC_POINT_free(card_point);
        EC_KEY_free(card_eckey);
        return false;
    }
    
    EC_KEY_set_public_key(card_eckey, card_point);
    EC_POINT_free(card_point);
    
    // Convert to EVP_PKEY
    d->cardPublicKey = EVP_PKEY_new();
    if (EVP_PKEY_set1_EC_KEY(d->cardPublicKey, card_eckey) != 1) {
        qWarning() << "SecureChannel: Failed to set card public key";
        EC_KEY_free(card_eckey);
        return false;
    }
    EC_KEY_free(card_eckey);
    
    // Step 4: Compute ECDH shared secret
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(d->privateKey, nullptr);
    if (!ctx) {
        qWarning() << "SecureChannel: Failed to create derivation context";
        return false;
    }
    
    if (EVP_PKEY_derive_init(ctx) <= 0) {
        qWarning() << "SecureChannel: Failed to init derive";
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    
    if (EVP_PKEY_derive_set_peer(ctx, d->cardPublicKey) <= 0) {
        qWarning() << "SecureChannel: Failed to set peer key";
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    
    // Determine buffer length
    size_t secret_len = 0;
    if (EVP_PKEY_derive(ctx, nullptr, &secret_len) <= 0) {
        qWarning() << "SecureChannel: Failed to determine secret length";
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    
    // Derive the shared secret
    d->secret.resize(static_cast<int>(secret_len));
    if (EVP_PKEY_derive(ctx, reinterpret_cast<unsigned char*>(d->secret.data()), 
                       &secret_len) <= 0) {
        qWarning() << "SecureChannel: Failed to derive shared secret";
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    
    EVP_PKEY_CTX_free(ctx);
    
    qDebug() << "SecureChannel: ECDH shared secret computed!" << d->secret.size() << "bytes";
    qDebug() << "SecureChannel: Secret:" << d->secret.toHex();
    
    return true;
#endif
}

void SecureChannel::init(const QByteArray& iv, const QByteArray& encKey, const QByteArray& macKey)
{
    qDebug() << "SecureChannel: Initializing session keys";
    qDebug() << "SecureChannel: IV:" << iv.toHex();
    qDebug() << "SecureChannel: Enc key:" << encKey.toHex() << "(" << encKey.size() << "bytes)";
    qDebug() << "SecureChannel: MAC key:" << macKey.toHex() << "(" << macKey.size() << "bytes)";
    
    // Go's DeriveSessionKeys returns encKey (32 bytes) and macKey (32 bytes)
    // Both use AES-256
    d->iv = iv;
    d->encKey = encKey;  // Full 32 bytes for AES-256
    d->macKey = macKey;  // Full 32 bytes for AES-256
    d->open = true;
    d->openedIndex = 0;
    
    qDebug() << "SecureChannel: Using Enc key:" << d->encKey.toHex();
    qDebug() << "SecureChannel: Using MAC key:" << d->macKey.toHex();
}

void SecureChannel::reset()
{
    qDebug() << "SecureChannel: Resetting session state (keeping ephemeral keys for pairing)";
    // Clear session keys
    d->iv.clear();
    d->encKey.clear();
    d->macKey.clear();
    d->open = false;
    d->openedIndex = -1;
    
    // NOTE: d->secret, d->rawPublicKeyData, and d->privateKey are kept
    // They're needed for OPEN_SECURE_CHANNEL after SELECT
    
#ifdef KEYCARD_QT_HAS_OPENSSL
    // Don't free privateKey - we need it for opening secure channel
    // if (d->privateKey) {
    //     EVP_PKEY_free(d->privateKey);
    //     d->privateKey = nullptr;
    // }
    if (d->cardPublicKey) {
        EVP_PKEY_free(d->cardPublicKey);
        d->cardPublicKey = nullptr;
    }
#endif
}

QByteArray SecureChannel::rawPublicKey() const
{
    return d->rawPublicKeyData;
}

QByteArray SecureChannel::secret() const
{
    return d->secret;
}

APDU::Response SecureChannel::send(const APDU::Command& command)
{
    // CRITICAL: Protect IV state from concurrent access by multiple threads
    // The IV is updated after each command send, and if two threads try to send
    // commands simultaneously, they will corrupt the IV state, causing the card
    // to reject commands with errors like 0x6f05 (invalid MAC).
    // This fixes the bug where PIN verification fails on first attempt when card
    // is inserted after app startup (UI thread calls getStatus while worker thread
    // calls verifyPIN, causing IV desynchronization).
    QMutexLocker locker(&m_secureMutex);
    
    // Log to verify the fix is applied (shows thread-safe operation)
    qDebug() << "🔒 SecureChannel: MUTEX PROTECTED send() - Thread:" << QThread::currentThread();
    qDebug() << "🔒 SecureChannel: IV at start of send():" << d->iv.toHex();
    
    if (!d->open) {
        throw std::runtime_error("Secure channel not open");
    }

    if (!d->channel) {
        throw std::runtime_error("No base channel available");
    }

    // DEBUG: Log the raw command before encryption
    qDebug() << "SecureChannel: Raw command CLA=" << QString("0x%1").arg(command.cla(), 2, 16, QChar('0'))
             << "INS=" << QString("0x%1").arg(command.ins(), 2, 16, QChar('0'))
             << "P1=" << QString("0x%1").arg(command.p1(), 2, 16, QChar('0'))
             << "P2=" << QString("0x%1").arg(command.p2(), 2, 16, QChar('0'))
             << "Lc=" << command.data().size()
             << "Le=" << (command.hasLe() ? QString::number(command.le()) : "none");
    qDebug() << "SecureChannel: Command data:" << command.data().toHex();

    // Encrypt only the command data (not the headers)
    QByteArray encData = encrypt(command.data());
    
    // Build metadata for MAC: [CLA, INS, P1, P2, len(encData)+16, 0x00...]
    QByteArray meta;
    meta.append(static_cast<char>(command.cla()));
    meta.append(static_cast<char>(command.ins()));
    meta.append(static_cast<char>(command.p1()));
    meta.append(static_cast<char>(command.p2()));
    meta.append(static_cast<char>(encData.size() + 16));  // +16 for IV/MAC
    meta.append(QByteArray(11, 0x00));  // Pad to 16 bytes total
    
    // Update IV with MAC computed over meta and encrypted_data
    QByteArray oldIV = d->iv;
    d->iv = calculateMAC(meta, encData);
    qDebug() << "🔒 SecureChannel: IV updated (BEFORE send):" << oldIV.toHex() << "->" << d->iv.toHex();
    
    // Build new data: [IV][encrypted_data]
    QByteArray newData = d->iv + encData;
    
    // Send command with original headers but new data
    APDU::Command secureCmd(command.cla(), command.ins(), command.p1(), command.p2());
    secureCmd.setData(newData);
    if (command.hasLe()) {
        secureCmd.setLe(command.le());
        qDebug() << "SecureChannel: Preserving Le from command:" << (int)command.le();
    } else {
        qDebug() << "SecureChannel: WARNING - Original command has NO Le set!";
    }
    
    // Send through base channel
    qDebug() << "SecureChannel: Sending encrypted APDU, Le=" << secureCmd.hasLe() << "value=" << (int)secureCmd.le();
    qDebug() << "SecureChannel: Encrypted APDU:" << secureCmd.serialize().toHex();
    QByteArray rawResponse = d->channel->transmit(secureCmd.serialize());
    qDebug() << "SecureChannel: Raw response from card:" << rawResponse.toHex();
    APDU::Response response(rawResponse);
    
    uint8_t sw1 = (response.sw() >> 8) & 0xFF;
    uint8_t sw2 = response.sw() & 0xFF;
    
    qDebug() << "SecureChannel: Raw SW from card:" << QString("0x%1").arg(response.sw(), 4, 16, QChar('0'));
    qDebug() << "SecureChannel: SW1:" << QString("0x%1").arg(sw1, 2, 16, QChar('0')) 
             << "SW2:" << QString("0x%1").arg(sw2, 2, 16, QChar('0'));
    
    // Handle multi-frame responses (SW1=0x61 = more data available)
    if (sw1 == 0x61) {
        qWarning() << "⚠️⚠️⚠️ SecureChannel: Card indicates MORE DATA AVAILABLE (SW1=0x61)!";
        qWarning() << "⚠️ Remaining bytes available:" << (int)sw2;
        qWarning() << "⚠️ Multi-frame response handling NOT IMPLEMENTED - data will be truncated!";
        qWarning() << "⚠️ This may be why EXPORT_KEY returns only 16 bytes!";
    }

    // Decrypt response if successful
    if (response.isOK() && !response.data().isEmpty()) {
        // Response format: [MAC][encrypted_data]
        qDebug() << "SecureChannel: Raw response size:" << response.data().size() << "bytes";
        qDebug() << "SecureChannel: Raw response hex:" << response.data().toHex();
        
        if (response.data().size() < 16) {
            throw std::runtime_error("Response too short");
        }
        
        QByteArray responseMac = response.data().left(16);
        QByteArray responseData = response.data().mid(16);
        
        qDebug() << "SecureChannel: Encrypted data size:" << responseData.size() << "bytes";
        qDebug() << "SecureChannel: Encrypted data hex:" << responseData.toHex();
        
        // Decrypt FIRST using current IV (before MAC calculation updates it)
        QByteArray decrypted = decrypt(responseData);
        
        qDebug() << "SecureChannel: Decrypted data size:" << decrypted.size() << "bytes";
        qDebug() << "SecureChannel: Decrypted data hex:" << decrypted.toHex();
        
        // Now calculate and verify MAC
        // IMPORTANT: rmeta size should include BOTH MAC and data (total response.data().size())
        QByteArray rmeta;
        rmeta.append(static_cast<char>(response.data().size()));  // Total size including MAC!
        rmeta.append(QByteArray(15, 0x00));
        
        QByteArray calculatedMac = calculateMAC(rmeta, responseData);
        
        if (calculatedMac != responseMac) {
            qWarning() << "SecureChannel: MAC mismatch!";
            throw std::runtime_error("Response MAC verification failed");
        }
        
        // Update IV for next operation
        QByteArray prevIV = d->iv;
        d->iv = calculatedMac;
        qDebug() << "🔒 SecureChannel: IV updated (AFTER recv):" << prevIV.toHex() << "->" << d->iv.toHex();
        
        // CRITICAL FIX: The decrypted response format is [data...][SW1][SW2]
        // The status word is AT THE END of the decrypted data, NOT separate.
        // Simply return the decrypted data as-is - it already contains the status word.
        // DO NOT append 9000 or we'll mask the real status word!
        return APDU::Response(decrypted);
    }
    
    return response;
}

QByteArray SecureChannel::encrypt(const QByteArray& plaintext)
{
#ifndef KEYCARD_QT_HAS_OPENSSL
    qWarning() << "SecureChannel: OpenSSL not available, cannot encrypt";
    return plaintext; // Fallback: return unencrypted
#else
    // OpenSSL AES-256-CBC encryption
    if (!d->open) {
        qWarning() << "SecureChannel: Channel not open";
        return QByteArray();
    }
    
    // Pad the plaintext
    QByteArray padded = APDU::Utils::pad(plaintext, 16);
    
    // Create cipher context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        qWarning() << "SecureChannel: Failed to create cipher context";
        return QByteArray();
    }
    
    // Initialize encryption with AES-256-CBC
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(d->encKey.constData()),
                           reinterpret_cast<const unsigned char*>(d->iv.constData())) != 1) {
        qWarning() << "SecureChannel: EVP_EncryptInit_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    // Disable padding (we already padded)
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    
    // Encrypt
    QByteArray encrypted(padded.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()), 0);
    int len = 0;
    if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(encrypted.data()), &len,
                          reinterpret_cast<const unsigned char*>(padded.constData()),
                          padded.size()) != 1) {
        qWarning() << "SecureChannel: EVP_EncryptUpdate failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(encrypted.data()) + len, &finalLen) != 1) {
        qWarning() << "SecureChannel: EVP_EncryptFinal_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    encrypted.resize(len + finalLen);
    EVP_CIPHER_CTX_free(ctx);
    
    return encrypted;
#endif
}

QByteArray SecureChannel::decrypt(const QByteArray& ciphertext)
{
#ifndef KEYCARD_QT_HAS_OPENSSL
    qWarning() << "SecureChannel: OpenSSL not available, cannot decrypt";
    return ciphertext; // Fallback: return as-is
#else
    // OpenSSL AES-256-CBC decryption
    if (!d->open) {
        qWarning() << "SecureChannel: Channel not open";
        return QByteArray();
    }
    
    if (ciphertext.isEmpty()) {
        return QByteArray();
    }
    
    // Create cipher context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        qWarning() << "SecureChannel: Failed to create cipher context for decryption";
        return QByteArray();
    }
    
    // Initialize decryption with AES-256-CBC
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(d->encKey.constData()),
                           reinterpret_cast<const unsigned char*>(d->iv.constData())) != 1) {
        qWarning() << "SecureChannel: EVP_DecryptInit_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    // Disable padding (we'll unpad manually)
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    
    // Decrypt
    QByteArray decrypted(ciphertext.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()), 0);
    int len = 0;
    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(decrypted.data()), &len,
                          reinterpret_cast<const unsigned char*>(ciphertext.constData()),
                          ciphertext.size()) != 1) {
        qWarning() << "SecureChannel: EVP_DecryptUpdate failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    int finalLen = 0;
    if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(decrypted.data()) + len, &finalLen) != 1) {
        qWarning() << "SecureChannel: EVP_DecryptFinal_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    decrypted.resize(len + finalLen);
    EVP_CIPHER_CTX_free(ctx);
    
    // Unpad manually
    return APDU::Utils::unpad(decrypted);
#endif
}

QByteArray SecureChannel::oneShotEncrypt(const QByteArray& data)
{
#ifndef KEYCARD_QT_HAS_OPENSSL
    qWarning() << "SecureChannel: OpenSSL not available, cannot perform one-shot encryption";
    return QByteArray();
#else
    // One-shot encryption for INIT command (uses shared secret directly)
    if (d->secret.isEmpty()) {
        qWarning() << "SecureChannel: No shared secret available";
        return QByteArray();
    }
    
    qDebug() << "SecureChannel: OneShotEncrypt - input data size:" << data.size();
    
    // Generate random IV
    QByteArray iv(16, 0);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), 16) != 1) {
        qWarning() << "SecureChannel: Failed to generate random IV";
        return QByteArray();
    }
    
    // Pad data
    QByteArray padded = APDU::Utils::pad(data, 16);
    qDebug() << "SecureChannel: OneShotEncrypt - padded size:" << padded.size();
    
    // Encrypt with AES-256-CBC using full 32-byte secret
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        qWarning() << "SecureChannel: Failed to create cipher context for one-shot encryption";
        return QByteArray();
    }

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(d->secret.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) != 1) {
        qWarning() << "SecureChannel: One-shot EVP_EncryptInit_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }

    // Disable padding (we already padded)
    EVP_CIPHER_CTX_set_padding(ctx, 0);

    // Encrypt
    QByteArray encrypted(padded.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()), 0);
    int len = 0;
    if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(encrypted.data()), &len,
                          reinterpret_cast<const unsigned char*>(padded.constData()),
                          padded.size()) != 1) {
        qWarning() << "SecureChannel: One-shot EVP_EncryptUpdate failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }

    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(encrypted.data()) + len, &finalLen) != 1) {
        qWarning() << "SecureChannel: One-shot EVP_EncryptFinal_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }

    encrypted.resize(len + finalLen);
    EVP_CIPHER_CTX_free(ctx);
    
    // Build result: [pubkey_len][pubkey][IV][ciphertext]
    QByteArray result;
    QByteArray pubKey = d->rawPublicKeyData;  // Use the stored public key
    result.append(static_cast<char>(pubKey.size()));  // 1 byte: pubkey length (0x41 = 65)
    result.append(pubKey);                             // 65 bytes: uncompressed pubkey
    result.append(iv);                                 // 16 bytes: IV
    result.append(encrypted);                          // N bytes: ciphertext
    
    qDebug() << "SecureChannel: OneShotEncrypt - result size:" << result.size()
             << "(1 +" << pubKey.size() << "+ 16 +" << encrypted.size() << ")";
    
    return result;
#endif
}

bool SecureChannel::isOpen() const
{
    return d->open;
}

QByteArray SecureChannel::calculateMAC(const QByteArray& meta, const QByteArray& data)
{
    // This implements keycard-go's CalculateMac function:
    // Go code: encrypts meta IN-PLACE, then data IN-PLACE with same CBC mode
    // This means: IV for data encryption = last block of encrypted meta
    // Extract MAC from second-to-last block of encrypted data
    
#ifndef KEYCARD_QT_HAS_OPENSSL
    qWarning() << "SecureChannel: OpenSSL not available for MAC calculation";
    return QByteArray(16, 0x00);
#else
    // OpenSSL AES-256-CBC MAC calculation
    // Pad data
    QByteArray paddedData = data;
    int paddingSize = 16 - (data.size() % 16);
    paddedData.append(static_cast<char>(0x80));
    if (paddingSize > 1) {
        paddedData.append(QByteArray(paddingSize - 1, 0x00));
    }
    
    // Step 1: Encrypt meta with zero IV
    QByteArray zeroIV(16, 0x00);
    EVP_CIPHER_CTX* ctx1 = EVP_CIPHER_CTX_new();
    if (!ctx1) {
        qWarning() << "SecureChannel: Failed to create cipher context for MAC meta";
        return QByteArray(16, 0x00);
    }
    
    if (EVP_EncryptInit_ex(ctx1, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(d->macKey.constData()),
                           reinterpret_cast<const unsigned char*>(zeroIV.constData())) != 1) {
        qWarning() << "SecureChannel: MAC meta EVP_EncryptInit_ex failed";
        EVP_CIPHER_CTX_free(ctx1);
        return QByteArray(16, 0x00);
    }
    
    EVP_CIPHER_CTX_set_padding(ctx1, 0);
    
    QByteArray encryptedMeta(meta.size() + 16, 0);
    int len1 = 0;
    if (EVP_EncryptUpdate(ctx1, reinterpret_cast<unsigned char*>(encryptedMeta.data()), &len1,
                          reinterpret_cast<const unsigned char*>(meta.constData()),
                          meta.size()) != 1) {
        qWarning() << "SecureChannel: MAC meta EVP_EncryptUpdate failed";
        EVP_CIPHER_CTX_free(ctx1);
        return QByteArray(16, 0x00);
    }
    
    int finalLen1 = 0;
    if (EVP_EncryptFinal_ex(ctx1, reinterpret_cast<unsigned char*>(encryptedMeta.data()) + len1, &finalLen1) != 1) {
        qWarning() << "SecureChannel: MAC meta EVP_EncryptFinal_ex failed";
        EVP_CIPHER_CTX_free(ctx1);
        return QByteArray(16, 0x00);
    }
    encryptedMeta.resize(len1 + finalLen1);
    EVP_CIPHER_CTX_free(ctx1);
    
    // Step 2: Use last block of encrypted meta as IV for data encryption
    QByteArray newIV = encryptedMeta.right(16);
    
    EVP_CIPHER_CTX* ctx2 = EVP_CIPHER_CTX_new();
    if (!ctx2) {
        qWarning() << "SecureChannel: Failed to create cipher context for MAC data";
        return QByteArray(16, 0x00);
    }
    
    if (EVP_EncryptInit_ex(ctx2, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(d->macKey.constData()),
                           reinterpret_cast<const unsigned char*>(newIV.constData())) != 1) {
        qWarning() << "SecureChannel: MAC data EVP_EncryptInit_ex failed";
        EVP_CIPHER_CTX_free(ctx2);
        return QByteArray(16, 0x00);
    }
    
    EVP_CIPHER_CTX_set_padding(ctx2, 0);
    
    QByteArray encryptedData(paddedData.size() + 16, 0);
    int len2 = 0;
    if (EVP_EncryptUpdate(ctx2, reinterpret_cast<unsigned char*>(encryptedData.data()), &len2,
                          reinterpret_cast<const unsigned char*>(paddedData.constData()),
                          paddedData.size()) != 1) {
        qWarning() << "SecureChannel: MAC data EVP_EncryptUpdate failed";
        EVP_CIPHER_CTX_free(ctx2);
        return QByteArray(16, 0x00);
    }
    
    int finalLen2 = 0;
    if (EVP_EncryptFinal_ex(ctx2, reinterpret_cast<unsigned char*>(encryptedData.data()) + len2, &finalLen2) != 1) {
        qWarning() << "SecureChannel: MAC data EVP_EncryptFinal_ex failed";
        EVP_CIPHER_CTX_free(ctx2);
        return QByteArray(16, 0x00);
    }
    encryptedData.resize(len2 + finalLen2);
    EVP_CIPHER_CTX_free(ctx2);
    
    // Extract second-to-last block (16 bytes) as MAC
    if (encryptedData.size() < 32) {
        qWarning() << "SecureChannel: Encrypted data too small for MAC extraction";
        return QByteArray(16, 0x00);
    }
    
    return encryptedData.mid(encryptedData.size() - 32, 16);
#endif
}

QByteArray SecureChannel::updateMAC(const QByteArray& data)
{
    // Legacy method - calls calculateMAC with empty meta
    return calculateMAC(QByteArray(16, 0x00), data);
}

bool SecureChannel::verifyMAC(const QByteArray& data, const QByteArray& receivedMAC)
{
    QByteArray computed = updateMAC(data);
    return computed == receivedMAC;
}

} // namespace Keycard

