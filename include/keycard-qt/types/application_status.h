#pragma once

#include <QByteArray>

namespace Keycard {

/**
 * @brief Application status returned by GET STATUS command
 */
class ApplicationStatus {
public:
    ApplicationStatus() = default;
    
    /**
     * @brief Parse application status from response data
     * @param data Raw response data
     * @return Parsed ApplicationStatus or error
     */
    static ApplicationStatus parse(const QByteArray& data, QString* error = nullptr);
    
    // Getters
    bool pinRetryCount() const { return m_pinRetryCount; }
    bool pukRetryCount() const { return m_pukRetryCount; }
    bool hasKey() const { return m_hasKey; }
    QByteArray keyPath() const { return m_keyPath; }
    
private:
    int m_pinRetryCount = 0;
    int m_pukRetryCount = 0;
    bool m_hasKey = false;
    QByteArray m_keyPath;
};

} // namespace Keycard

