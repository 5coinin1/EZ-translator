#pragma once

#include "translation/ITranslator.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QProcess>
#include <QString>

namespace EZTranslator {

/**
 * @brief Engine dịch offline qua CTranslate2, chạy trong một tiến trình Python phụ.
 *
 * CTranslate2 chỉ phát hành wheel Python (không có thư viện C++ prebuilt cho
 * Windows), nên app nói chuyện với tiến trình con `ct2_server.py` bằng JSON mỗi
 * dòng: nhanh (int8 ~15-40ms/câu) và không cần build gì thêm.
 *
 * Sidecar chết → tự khởi động lại ở lần dịch kế tiếp (có giãn cách tối thiểu để
 * không thrash), và mọi lượt dịch đều có timeout nên một câu treo không khoá
 * được cả luồng dịch.
 */
class CTranslate2Translator final : public ITranslator
{
public:
    CTranslate2Translator();
    ~CTranslate2Translator() override;

    CTranslate2Translator(const CTranslate2Translator&) = delete;
    CTranslate2Translator& operator=(const CTranslate2Translator&) = delete;

    /**
     * @brief Khởi động sidecar.
     * @return false nếu thiếu Python/ctranslate2 hoặc model (khi đó engine coi như
     *         không dùng được và caller nên rơi về engine khác).
     */
    bool start(const QString& pythonExe, const QString& scriptPath, const QString& modelDir,
               const QString& computeType, const QString& sourceLang, const QString& targetLang,
               QString* error = nullptr);

    void stop();

    [[nodiscard]] bool isReady() const override;
    [[nodiscard]] QString name() const override { return QStringLiteral("Offline (CT2 int8)"); }

    [[nodiscard]] QStringList translate(const QStringList& lines, const QString& source,
                                        const QString& target) override;

    /** Python nào dùng được (đường dẫn launcher/exe) — rỗng nếu không tìm thấy. */
    [[nodiscard]] static QString findPython();

private:
    // Giữ ống stderr của sidecar luôn rỗng (sidecar chặn cứng nếu ống đầy) + ghi log chẩn đoán
    // vào %TEMP%/ez_ct2.log.
    void drainServerLog();
    bool ensureStarted(QString* error);
    bool waitForReadyLine(int timeoutMs, QString* error);
    void attachToJob();
    bool writeRequest(const QByteArray& payload);
    bool readReply(QStringList* translations);
    void markFailed(const QString& reason);

    QProcess m_process;
    QByteArray m_buffer;
    QString m_python;
    QString m_script;
    QString m_modelDir;
    QString m_computeType{QStringLiteral("int8")};
    QString m_sourceLang;
    QString m_targetLang;
    bool m_ready{false};
    QElapsedTimer m_clock;
    qint64 m_lastStartAttemptMs{-1000000}; ///< giãn cách giữa các lần thử khởi động lại
    void* m_job{nullptr};                  ///< Windows Job Object (sidecar chết theo app)
};

} // namespace EZTranslator
