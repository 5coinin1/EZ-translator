#include "OfflineTranslator.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

OfflineTranslator::OfflineTranslator(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);

    connect(&m_timer, &QTimer::timeout, this,
            [this]()
            { fail(QStringLiteral("Bo dich phan hoi qua lau.")); });

    connect(&m_process, &QProcess::readyReadStandardOutput, this,
            &OfflineTranslator::readOutput);

    connect(&m_process, &QProcess::readyReadStandardError, this, [this]()
            { m_stderr = (m_stderr + m_process.readAllStandardError()).right(4096); });

    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError)
            { fail(m_process.errorString()); });

    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus)
            {
            m_stderr += m_process.readAllStandardError();

            fail(QStringLiteral("Python da thoat (ma %1). %2")
                     .arg(code)
                     .arg(QString::fromUtf8(m_stderr.right(4096)))); });
}

OfflineTranslator::~OfflineTranslator()
{
    m_timer.stop();
    m_process.disconnect(this);

    if (m_process.state() != QProcess::NotRunning)
    {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

bool OfflineTranslator::start(const QString &root, QString *error)
{
    if (m_process.state() != QProcess::NotRunning)
    {
        if (!m_failed)
            return true;

        if (error)
            *error = QStringLiteral("Bo dich dang dong; hay thu lai.");

        return false;
    }

    const QDir dir(root);

#ifdef Q_OS_WIN
    const QString python = dir.filePath(".venv-translation/Scripts/python.exe");
#else
    const QString python = dir.filePath(".venv-translation/bin/python");
#endif

    const QString script = dir.filePath("tools/translation_worker.py");

    const QString model = dir.filePath("models/translation/en_vi");

    for (const QString &path :
         {python, script, model + "/metadata.json",
          model + "/sentencepiece.model", model + "/model/model.bin"})
    {
        if (!QFileInfo::exists(path))
        {
            if (error)
                *error = QStringLiteral("Thieu file: ") + path;

            return false;
        }
    }

    cancel();

    m_ready = false;
    m_busy = false;
    m_failed = false;
    m_buffer.clear();
    m_stderr.clear();

    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUTF8", "1");
    env.insert("PYTHONIOENCODING", "utf-8");

    m_process.setProcessEnvironment(env);
    m_process.setWorkingDirectory(root);

    m_timer.start(60000);
    m_process.start(python, {"-u", script, "--model", model});

    if (m_failed)
    {
        if (error)
            *error = m_process.errorString();

        return false;
    }

    return true;
}

void OfflineTranslator::cancel()
{
    // Đổi mã yêu cầu để kết quả đang chạy trở thành kết quả cũ.
    m_latestId = QString::number(++m_serial);

    m_pendingId.clear();
    m_pendingText.clear();
}

void OfflineTranslator::submit(const QString &text)
{
    if (text.trimmed().isEmpty())
    {
        cancel();
        return;
    }

    m_latestId = QString::number(++m_serial);

    // Thay yêu cầu đang chờ bằng nội dung mới nhất.
    m_pendingId = m_latestId;
    m_pendingText = text;

    dispatch();
}

void OfflineTranslator::dispatch()
{
    if (!m_ready || m_busy || m_failed || m_pendingId.isEmpty())
        return;

    m_activeId = m_pendingId;
    m_activeText = m_pendingText;

    m_pendingId.clear();
    m_pendingText.clear();
    m_busy = true;

    QJsonObject request{{"id", m_activeId},
                        {"source", "en"},
                        {"target", "vi"},
                        {"texts", QJsonArray{m_activeText}}};

    QByteArray bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);

    bytes.append('\n');
    m_timer.start(60000);

    if (m_process.write(bytes) != bytes.size())
        fail(QStringLiteral("Khong gui duoc van ban sang Python."));
}

void OfflineTranslator::readOutput()
{
    m_buffer += m_process.readAllStandardOutput();

    if (m_buffer.size() > 1024 * 1024)
    {
        fail(QStringLiteral("Phan hoi Python qua lon."));
        return;
    }

    while (!m_failed)
    {
        const qsizetype end = m_buffer.indexOf('\n');

        if (end < 0)
            break;

        const QByteArray line = m_buffer.left(end).trimmed();
        m_buffer.remove(0, end + 1);

        if (line.isEmpty())
            continue;

        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(line, &error);

        if (error.error != QJsonParseError::NoError || !document.isObject())
        {
            fail(QStringLiteral("Python tra ve JSON khong hop le."));
            return;
        }

        const QJsonObject obj = document.object();
        const QString type = obj.value("type").toString();

        if (type == "ready")
        {
            if (m_ready || obj.value("source").toString() != "en" ||
                obj.value("target").toString() != "vi")
            {
                fail(QStringLiteral("Sai thong tin khoi dong bo dich."));
                return;
            }

            m_timer.stop();
            m_ready = true;

            emit ready();
            dispatch();
        }
        else if (type == "result")
        {
            const QJsonArray values = obj.value("translations").toArray();

            if (!m_busy || obj.value("id").toString() != m_activeId ||
                values.size() != 1 || !values.at(0).isString())
            {
                fail(QStringLiteral("Ket qua dich khong khop yeu cau."));
                return;
            }

            const QString id = m_activeId;
            const QString source = m_activeText;
            const QString target = values.at(0).toString();

            m_busy = false;
            m_timer.stop();

            if (id == m_latestId)
                emit translated(source, target);

            dispatch();
        }
        else if (type == "error")
        {
            fail(obj.value("error").toString(QStringLiteral("Loi bo dich.")));
            return;
        }
        else
        {
            fail(QStringLiteral("Khong nhan ra loai phan hoi Python."));
            return;
        }
    }
}

void OfflineTranslator::fail(const QString &message)
{
    if (m_failed)
        return;

    m_failed = true;
    m_ready = false;
    m_busy = false;

    m_timer.stop();
    cancel();

    if (m_process.state() != QProcess::NotRunning)
        m_process.kill();

    emit failed(message);
}