#pragma once

#include "types.h"
#include "types_parser.h"
#include "channel_interface.h"
#include "secure_channel.h"
#include "apdu/command.h"
#include "apdu/response.h"
#include <memory>
#include <QSharedPointer>

namespace Keycard {

/**
 * @brief High-level command set for Keycard operations
 * 
 * Provides convenient methods for all Keycard APDU commands.
 * Handles secure channel management and response parsing.
 */
class CommandSet {
public:
    explicit CommandSet(IChannel* channel);
    ~CommandSet();
    
    // Connection and pairing
    /**
     * @brief Select the Keycard applet
     * @return ApplicationInfo on success
     */
    ApplicationInfo select();
    
    /**
     * @brief Pair with the card using pairing password
     * @param pairingPassword Password for pairing (5-25 chars)
     * @return PairingInfo on success
     */
    PairingInfo pair(const QString& pairingPassword);
    
    /**
     * @brief Open secure channel with paired card
     * @param pairingInfo Previously obtained pairing info
     * @return true on success
     */
    bool openSecureChannel(const PairingInfo& pairingInfo);
    bool mutualAuthenticate();  // Mutual authentication after opening secure channel
    
    /**
     * @brief Initialize a new keycard
     * @param secrets PIN, PUK, and pairing password
     * @return true on success
     */
    bool init(const Secrets& secrets);
    
    /**
     * @brief Unpair a pairing slot
     * @param index Pairing slot index to unpair
     * @return true on success
     */
    bool unpair(uint8_t index);
    
    // Status and verification
    /**
     * @brief Get application status
     * @param info Status info type (P1 parameter)
     * @return ApplicationStatus on success
     */
    ApplicationStatus getStatus(uint8_t info = APDU::P1GetStatusApplication);
    
    /**
     * @brief Verify PIN
     * ⚠️ WARNING: 3 wrong attempts will BLOCK the PIN! Use with extreme caution!
     * Always call getStatus() first to check remaining attempts.
     * @param pin 6-digit PIN
     * @return true on success, false if wrong PIN (check remaining attempts)
     */
    bool verifyPIN(const QString& pin);
    
    // Security operations
    /**
     * @brief Change PIN (requires secure channel + previous PIN verification)
     * @param newPIN New 6-digit PIN
     * @return true on success
     */
    bool changePIN(const QString& newPIN);
    
    /**
     * @brief Change PUK (requires secure channel + previous PIN verification)
     * @param newPUK New 12-digit PUK
     * @return true on success
     */
    bool changePUK(const QString& newPUK);
    
    /**
     * @brief Unblock PIN using PUK
     * ⚠️ WARNING: 5 wrong PUK attempts will permanently block the card!
     * @param puk 12-digit PUK
     * @param newPIN New 6-digit PIN
     * @return true on success
     */
    bool unblockPIN(const QString& puk, const QString& newPIN);
    
    /**
     * @brief Change pairing password
     * @param newPassword New pairing password
     * @return true on success
     */
    bool changePairingSecret(const QString& newPassword);
    
    // Key management
    /**
     * @brief Generate a new key pair on the card
     * @return Key UID (32 bytes) on success
     */
    QByteArray generateKey();
    
    /**
     * @brief Generate BIP39 mnemonic on card
     * @param checksumSize Checksum size (4, 8, or 0 for random)
     * @return List of word indexes
     */
    QVector<int> generateMnemonic(int checksumSize = 4);
    
    /**
     * @brief Load seed to card
     * @param seed BIP39 seed (64 bytes)
     * @return Key UID on success
     */
    QByteArray loadSeed(const QByteArray& seed);
    
    /**
     * @brief Remove key from card
     * @return true on success
     */
    bool removeKey();
    
    /**
     * @brief Derive key at BIP32 path
     * @param path Derivation path (e.g., "m/44'/0'/0'/0/0")
     * @return true on success
     */
    bool deriveKey(const QString& path);
    
    // Signing
    /**
     * @brief Sign data with current key
     * @param data 32-byte hash to sign
     * @return Signature (65 bytes: R + S + V) on success
     */
    QByteArray sign(const QByteArray& data);
    
    /**
     * @brief Sign data with key at specific path
     * @param data 32-byte hash to sign
     * @param path Derivation path
     * @param makeCurrent If true, derived key becomes current
     * @return Signature on success
     */
    QByteArray signWithPath(const QByteArray& data, const QString& path, bool makeCurrent = false);
    
    /**
     * @brief Sign data with key at specific path, returning full TLV response
     * @param data 32-byte hash to sign
     * @param path Derivation path
     * @param makeCurrent If true, derived key becomes current
     * @return Full TLV response (includes public key and signature) on success
     */
    QByteArray signWithPathFullResponse(const QByteArray& data, const QString& path, bool makeCurrent = false);
    
    /**
     * @brief Sign without PIN (if pinless path set)
     * @param data 32-byte hash to sign
     * @return Signature on success
     */
    QByteArray signPinless(const QByteArray& data);
    
    /**
     * @brief Set path for pinless signing
     * @param path Absolute derivation path (must start with "m/")
     * @return true on success
     */
    bool setPinlessPath(const QString& path);
    
    // Data storage
    /**
     * @brief Store data on card
     * @param type Data type (0x00=public, 0x01=NDEF, 0x02=cash)
     * @param data Data to store
     * @return true on success
     */
    bool storeData(uint8_t type, const QByteArray& data);
    
    /**
     * @brief Get data from card
     * @param type Data type
     * @return Data on success
     */
    QByteArray getData(uint8_t type);
    
    // Utilities
    /**
     * @brief Identify the card
     * @param challenge Optional 32-byte challenge
     * @return Card identification
     */
    QByteArray identify(const QByteArray& challenge = QByteArray());
    
    /**
     * @brief Export public key (or private+public)
     * @param derive If true, derive first
     * @param makeCurrent If true and derive=true, make it current
     * @param path Derivation path (if derive=true)
     * @param exportType Export type (P2ExportKeyPublicOnly, P2ExportKeyPrivateAndPublic, etc.)
     * @return Key data (TLV encoded)
     */
    QByteArray exportKey(bool derive = false, bool makeCurrent = false, const QString& path = QString(), uint8_t exportType = APDU::P2ExportKeyPublicOnly);
    
    /**
     * @brief Export extended key (public or private+public)
     * @param derive If true, derive first
     * @param makeCurrent If true and derive=true, make it current
     * @param path Derivation path (if derive=true)
     * @param exportType Export type (P2ExportKeyPublicOnly, P2ExportKeyPrivateAndPublic, etc.)
     * @return Extended key data (TLV encoded)
     */
    QByteArray exportKeyExtended(bool derive = false, bool makeCurrent = false, const QString& path = QString(), uint8_t exportType = APDU::P2ExportKeyExtendedPublic);
    
    /**
     * @brief Factory reset the card
     * ⚠️ WARNING: This will erase all data on the card permanently!
     * @return true on success
     */
    bool factoryReset();
    
    /**
     * @brief Get last error message
     * @return Error message
     */
    QString lastError() const { return m_lastError; }
    
    /**
     * @brief Get remaining PIN attempts (after failed verifyPIN)
     * @return Remaining attempts, or -1 if not applicable
     */
    int remainingPINAttempts() const { return m_remainingPINAttempts; }
    
    // Accessors
    ApplicationInfo applicationInfo() const { return m_appInfo; }
    PairingInfo pairingInfo() const { return m_pairingInfo; }
    
    // Test helpers (for unit testing only - bypasses crypto validation)
    #ifdef KEYCARD_ENABLE_TEST_HELPERS
    /**
     * @brief Directly inject secure channel state for testing
     * @param pairingInfo Mock pairing info
     * @param iv Mock initialization vector (16 bytes)
     * @param encKey Mock encryption key (16 bytes)
     * @param macKey Mock MAC key (16 bytes)
     * 
     * WARNING: This bypasses all cryptographic validation and should
     * ONLY be used in unit tests to test business logic without requiring
     * real cryptographic operations.
     */
    void testInjectSecureChannelState(const PairingInfo& pairingInfo,
                                       const QByteArray& iv,
                                       const QByteArray& encKey,
                                       const QByteArray& macKey) {
        m_pairingInfo = pairingInfo;
        m_secureChannel->init(iv, encKey, macKey);
    }
    #endif
    
private:
    // Helper methods
    bool checkOK(const APDU::Response& response);
    APDU::Command buildCommand(uint8_t ins, uint8_t p1 = 0, uint8_t p2 = 0, 
                                const QByteArray& data = QByteArray());
    
    IChannel* m_channel;
    QSharedPointer<SecureChannel> m_secureChannel;
    ApplicationInfo m_appInfo;
    PairingInfo m_pairingInfo;
    QString m_lastError;
    int m_remainingPINAttempts = -1;
};

} // namespace Keycard

