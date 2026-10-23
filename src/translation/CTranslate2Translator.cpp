#include "translation/CTranslate2Translator.h"
#include "translation/TranslationThreadBudget.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace EZTranslator {

namespace {

constexpr int kStartTimeoutMs = 30000;   ///< Python + model nạp có thể lâu
constexpr int kReplyTimeoutMs = 2500;    ///< một lượt dịch; quá hạn coi như sidecar chết
constexpr int kWriteTimeoutMs = 1000;
constexpr int kRestartIntervalMs = 5000; ///< giãn cách thử khởi động lại
constexpr int kProbeTimeoutMs = 20000;   ///< kiểm tra "import ctranslate2"

/** Đường dẫn script sidecar: bản cạnh .exe (khi phát hành) trước, rồi bản trong source. */
QString ct2ScriptPath()
{
    const QString beside =
        QCoreApplication::applicationDirPath() + QStringLiteral("/ct2_server.py");
    if (QFileInfo::exists(beside))
        return beside;
#ifdef EZ_CT2_SCRIPT
    return QStringLiteral(EZ_CT2_SCRIPT);
#else
    return beside;
#endif
}

} // namespace

CTranslate2Translator::CTranslate2Translator()
{
    m_clock.start();
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
}

void CTranslate2Translator::drainServerLog()
{
    // Sidecar ghi log/lỗi ra stderr. Không ai đọc thì ống đầy và tiến trình Python CHẶN CỨNG ở lần
    // ghi kế tiếp (deadlock tới hết timeout — đã gặp thật ở bản translator: "tự nhiên không dịch").
    // Giữ ống rỗng ở MỌI lần chờ phản hồi; nội dung ghi lại để chẩn đoán.
    const QByteArray log = m_process.readAllStandardError();
    if (log.isEmpty())
        return;
    QFile file(QDir::tempPath() + QStringLiteral("/ez_ct2.log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.write(log);
}

CTranslate2Translator::~CTranslate2Translator()
{
    stop();
#if defined(_WIN32)
    // Đóng Job Object → mọi sidecar còn sót trong job bị kill theo (kể cả khi app
    // thoát bất thường trước đó).
    if (m_job) {
        CloseHandle(reinterpret_cast<HANDLE>(m_job));
        m_job = nullptr;
    }
#endif
}

void CTranslate2Translator::attachToJob()
{
#if defined(_WIN32)
    if (!m_job) {
        // KILL_ON_JOB_CLOSE: tiến trình con chết cùng app, không để lại Python mồ côi.
        m_job = CreateJobObjectW(nullptr, nullptr);
        if (!m_job)
            return;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(reinterpret_cast<HANDLE>(m_job),
                                JobObjectExtendedLimitInformation, &info, sizeof(info));
    }
    AssignProcessToJobObject(reinterpret_cast<HANDLE>(m_job),
                             reinterpret_cast<HANDLE>(qintptr(m_process.processId())));
#endif
}

QString CTranslate2Translator::findPython()
{
    // Ưu tiên Python riêng của project khi bản cài đặt nằm cạnh thư mục build.
    QStringList candidates;
    const QString fromEnv = qEnvironmentVariable("EZ_PYTHON");
    if (!fromEnv.isEmpty())
        candidates.append(fromEnv);
    const QString localPython = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("../.venv/Scripts/python.exe"));
    if (QFileInfo::exists(localPython))
        candidates.append(QFileInfo(localPython).absoluteFilePath());
    for (const QString& name : {QStringLiteral("py"), QStringLiteral("python"),
                               QStringLiteral("python3")}) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty() && !candidates.contains(path))
            candidates.append(path);
    }

    // Chỉ nhận interpreter thật sự có ctranslate2 + sentencepiece: nhiều máy có
    // "python" trỏ vào bản khác (vd msys2) không cài hai gói này.
    for (const QString& candidate : candidates) {
        QProcess probe;
        probe.start(candidate, {QStringLiteral("-c"),
                                QStringLiteral("import ctranslate2, sentencepiece")});
        if (!(probe.waitForFinished(kProbeTimeoutMs)
              && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0)) {
            continue;
        }

        // Trả về chính python.exe thật chứ KHÔNG phải launcher `py`: launcher lại sinh
        // thêm một tiến trình con nữa, nên theo dõi/kill nó có thể để python mồ côi.
        QProcess resolve;
        resolve.start(candidate,
                      {QStringLiteral("-c"), QStringLiteral("import sys; print(sys.executable)")});
        if (resolve.waitForFinished(kProbeTimeoutMs)
            && resolve.exitStatus() == QProcess::NormalExit) {
            const QString real =
                QString::fromLocal8Bit(resolve.readAllStandardOutput()).trimmed();
            if (!real.isEmpty() && QFileInfo::exists(real))
                return real;
        }
        return candidate;
    }
    return {};
}

bool CTranslate2Translator::isReady() const
{
    return m_ready && m_process.state() == QProcess::Running;
}

void CTranslate2Translator::stop()
{
    m_ready = false;
    m_buffer.clear();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(2000))
            m_process.kill();
    }
}

void CTranslate2Translator::markFailed(const QString& reason)
{
    if (!reason.isEmpty())
        qWarning().noquote() << "[CT2]" << reason;
    m_ready = false;
    m_buffer.clear();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(500);
    }
}

bool CTranslate2Translator::start(const QString& pythonExe, const QString& scriptPath,
                                  const QString& modelDir, const QString& computeType,
                                  const QString& sourceLang, const QString& targetLang,
                                  QString* error)
{
    const QString resolved = scriptPath.isEmpty() ? ct2ScriptPath() : scriptPath;
    // Đổi model/ngôn ngữ thì phải khởi động lại, nếu không sidecar cũ vẫn phục vụ
    // model cũ (đổi ngôn ngữ sẽ im lặng không có tác dụng).
    if (isReady()
        && (m_python != pythonExe || m_script != resolved || m_modelDir != modelDir
            || m_computeType != computeType || m_sourceLang != sourceLang
            || m_targetLang != targetLang)) {
        stop();
    }
    m_python = pythonExe;
    m_script = resolved;
    m_modelDir = modelDir;
    m_computeType = computeType;
    m_sourceLang = sourceLang;
    m_targetLang = targetLang;
    return ensureStarted(error);
}

bool CTranslate2Translator::ensureStarted(QString* error)
{
    if (isReady())
        return true;

    const qint64 now = m_clock.elapsed();
    if (now - m_lastStartAttemptMs < kRestartIntervalMs) {
        if (error)
            *error = QStringLiteral("sidecar vừa lỗi, chờ trước khi thử lại");
        return false;
    }
    m_lastStartAttemptMs = now;

    if (m_python.isEmpty() || !QFileInfo::exists(m_python)) {
        if (error)
            *error = QStringLiteral("không tìm thấy Python có ctranslate2 (đặt EZ_PYTHON)");
        return false;
    }
    if (!QFileInfo::exists(m_script)) {
        if (error)
            *error = QStringLiteral("không thấy ct2_server.py: ") + m_script;
        return false;
    }
    if (!QDir(m_modelDir).exists()) {
        if (error)
            *error = QStringLiteral("không thấy model CTranslate2: ") + m_modelDir;
        return false;
    }

    markFailed(QString());
    m_process.setWorkingDirectory(QFileInfo(m_script).absolutePath());
    const int threads = chooseTranslationThreads();
    qInfo() << "[Translate] CT2 thread budget =" << threads;
    m_process.start(m_python,
                    {m_script, m_modelDir, m_computeType, QString::number(threads), m_sourceLang,
                     m_targetLang});
    if (!m_process.waitForStarted(kStartTimeoutMs)) {
        if (error)
            *error = QStringLiteral("không chạy được Python: ") + m_python;
        markFailed(QString());
        return false;
    }
    if (!waitForReadyLine(kStartTimeoutMs, error)) {
        markFailed(QString());
        return false;
    }
    attachToJob();
    m_ready = true;
    return true;
}

bool CTranslate2Translator::waitForReadyLine(int timeoutMs, QString* error)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired()) {
        m_buffer += m_process.readAllStandardOutput();
        int newline = -1;
        while ((newline = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(newline).trimmed();
            m_buffer.remove(0, newline + 1);
            if (line.isEmpty())
                continue;
            const QJsonObject object = QJsonDocument::fromJson(line).object();
            if (object.contains(QStringLiteral("ready")))
                return true;
        }
        if (m_process.state() == QProcess::NotRunning)
            break;
        const qint64 remaining = qMax<qint64>(50, deadline.remainingTime());
        if (!m_process.waitForReadyRead(int(remaining)) && m_process.state() == QProcess::NotRunning)
            break;
    }

    if (error) {
        QString detail = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
        if (detail.isEmpty())
            detail = QStringLiteral("sidecar không báo sẵn sàng");
        *error = detail;
    }
    return false;
}

bool CTranslate2Translator::writeRequest(const QByteArray& payload)
{
    if (m_process.write(payload) != payload.size())
        return false;
    return m_process.waitForBytesWritten(kWriteTimeoutMs);
}

bool CTranslate2Translator::readReply(QStringList* translations)
{
    QDeadlineTimer deadline(kReplyTimeoutMs);
    while (!deadline.hasExpired()) {
        m_buffer += m_process.readAllStandardOutput();
        int newline = -1;
        while ((newline = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(newline).trimmed();
            m_buffer.remove(0, newline + 1);
            if (line.isEmpty())
                continue;

            const QJsonObject object = QJsonDocument::fromJson(line).object();
            if (object.contains(QStringLiteral("ready"))) {
                m_ready = true;
                continue;
            }
            if (object.contains(QStringLiteral("translations"))) {
                const QJsonArray array = object.value(QStringLiteral("translations")).toArray();
                translations->clear();
                translations->reserve(array.size());
                for (const QJsonValue& value : array)
                    translations->append(value.toString());
                return true;
            }
        }
        if (m_process.state() == QProcess::NotRunning)
            return false;
        drainServerLog();
        const qint64 remaining = qMax<qint64>(50, deadline.remainingTime());
        if (!m_process.waitForReadyRead(int(remaining)) && m_process.state() == QProcess::NotRunning)
            return false;
    }
    return false;
}

QStringList CTranslate2Translator::translate(const QStringList& lines, const QString& /*source*/,
                                             const QString& /*target*/)
{
    QStringList result;
    result.resize(lines.size()); // rỗng = dòng đó lỗi, caller rơi về engine khác
    if (lines.isEmpty())
        return result;

    QString error;
    if (!isReady() && !ensureStarted(&error)) {
        if (!error.isEmpty())
            qWarning().noquote() << "[CT2] không sẵn sàng:" << error;
        return result;
    }

    QJsonArray texts;
    for (const QString& line : lines)
        texts.append(line);
    QJsonObject request;
    request.insert(QStringLiteral("texts"), texts);
    const QByteArray payload =
        QJsonDocument(request).toJson(QJsonDocument::Compact) + QByteArrayLiteral("\n");

    // Một lần thử lại tại chỗ nếu sidecar vừa chết (vd bị kill từ bên ngoài).
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!writeRequest(payload)) {
            markFailed(QStringLiteral("không ghi được yêu cầu (sidecar chết?)"));
            if (attempt == 0 && ensureStarted(&error))
                continue;
            return result;
        }
        QStringList translated;
        if (readReply(&translated)) {
            for (int i = 0; i < translated.size() && i < result.size(); ++i)
                result[i] = translated.at(i);
            return result;
        }
        markFailed(QStringLiteral("hết hạn chờ phản hồi (") + QString::number(kReplyTimeoutMs)
                   + QStringLiteral("ms)"));
        if (attempt == 0 && ensureStarted(&error))
            continue;
        return result;
    }
    return result;
}

} // namespace EZTranslator
