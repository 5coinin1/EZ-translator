#include "translation/AiTranslator.h"

#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace EZTranslator {

namespace {

constexpr int kTimeoutMs = 45000; ///< LLM có thể chậm hơn engine dịch thường

/** Bỏ rào markdown ```json ... ``` mà nhiều model vẫn tự thêm. */
QString stripCodeFence(QString text)
{
    text = text.trimmed();
    if (!text.startsWith(QLatin1String("```")))
        return text;

    const int firstNewline = text.indexOf(QLatin1Char('\n'));
    if (firstNewline < 0)
        return text.mid(3).trimmed();

    text = text.mid(firstNewline + 1);
    if (text.trimmed().endsWith(QLatin1String("```")))
        text.chop(3);
    return text.trimmed();
}

/** Đọc nội dung trả lời từ các biến thể response của OpenAI-compatible API. */
QString extractContent(const QJsonObject& root)
{
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return {};

    const QJsonObject choice = choices.first().toObject();
    const QJsonValue content = choice.value(QStringLiteral("message"))
                                   .toObject()
                                   .value(QStringLiteral("content"));

    if (content.isString())
        return content.toString();

    // Một số provider trả content dạng mảng part: [{"type":"text","text":"..."}]
    if (content.isArray()) {
        QString text;
        for (const QJsonValue& part : content.toArray()) {
            if (part.isString())
                text += part.toString();
            else if (part.isObject())
                text += part.toObject().value(QStringLiteral("text")).toString();
        }
        return text;
    }

    // Endpoint completion kiểu cũ: choices[0].text
    return choice.value(QStringLiteral("text")).toString();
}

/**
 * @brief Parse output của model thành đúng `expected` chuỗi.
 *
 * Ưu tiên mảng JSON; nếu model trả text thuần (1 đoạn) hoặc danh sách có đánh
 * số thì vẫn cố cứu để không mất bản dịch.
 */
QStringList parseTranslations(const QString& raw, int expected)
{
    const QString cleaned = stripCodeFence(raw);

    const int start = cleaned.indexOf(QLatin1Char('['));
    const int end = cleaned.lastIndexOf(QLatin1Char(']'));
    if (start >= 0 && end > start) {
        QJsonParseError error{};
        const QJsonDocument document =
            QJsonDocument::fromJson(cleaned.mid(start, end - start + 1).toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && document.isArray()) {
            QStringList out;
            out.reserve(expected);
            for (const QJsonValue& value : document.array()) {
                if (value.isString())
                    out.append(value.toString());
                else if (value.isObject())
                    out.append(value.toObject().value(QStringLiteral("text")).toString());
                else
                    out.append(QString());
            }
            if (!out.isEmpty()) {
                while (out.size() < expected)
                    out.append(QString());
                return out.mid(0, expected);
            }
        }
    }

    // Model trả thẳng bản dịch của một đoạn.
    if (expected == 1) {
        QString single = cleaned;
        if (single.size() >= 2 && single.startsWith(QLatin1Char('"'))
            && single.endsWith(QLatin1Char('"'))) {
            single = single.mid(1, single.size() - 2);
            single.replace(QLatin1String("\\n"), QStringLiteral("\n"));
            single.replace(QLatin1String("\\\""), QStringLiteral("\""));
            single.replace(QLatin1String("\\\\"), QStringLiteral("\\"));
        }
        single = single.trimmed();
        return single.isEmpty() ? QStringList() : QStringList{single};
    }

    // Fallback: model trả mỗi dòng một bản dịch (có thể kèm "-" hoặc "1.").
    QStringList lines = cleaned.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() == expected) {
        static const QRegularExpression prefix(
            QStringLiteral("^\\s*(?:[-*•]|\\d+[.)])\\s*"));
        QStringList out;
        out.reserve(expected);
        for (QString line : lines) {
            line.remove(prefix);
            out.append(line.trimmed());
        }
        return out;
    }

    return {};
}

QString buildSystemPrompt(const QString& userPrompt, const QString& source, const QString& target)
{
    const QString base = userPrompt.trimmed().isEmpty()
                             ? QStringLiteral("Bạn là một dịch giả chuyên nghiệp, am hiểu game, "
                                              "truyện và hội thoại. Dịch tự nhiên, giữ đúng sắc "
                                              "thái và tên riêng.")
                             : userPrompt.trimmed();

    const QString sourceName =
        (source.isEmpty() || source == QLatin1String("auto"))
            ? QStringLiteral("ngôn ngữ nguồn (tự nhận diện)")
            : source;
    const QString targetName = target.isEmpty() ? QStringLiteral("tiếng Việt") : target;

    return base + QStringLiteral("\n\n=== QUY TẮC ĐẦU RA ===\n")
           + QStringLiteral("Bạn nhận một mảng JSON gồm các đoạn văn cần dịch từ ")
           + sourceName + QStringLiteral(" sang ") + targetName
           + QStringLiteral(".\nTrả về DUY NHẤT một mảng JSON các chuỗi đã dịch, cùng số "
                            "phần tử và cùng thứ tự với mảng đầu vào. Không thêm markdown, "
                            "không giải thích, không ghi số thứ tự. Giữ tên riêng, thuật ngữ "
                            "game và sắc thái hội thoại.");
}

} // namespace

struct AiTranslator::Impl
{
    Options options;
    QNetworkAccessManager manager;
};

AiTranslator::AiTranslator(Options options)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->options = std::move(options);
}

AiTranslator::~AiTranslator() = default;

bool AiTranslator::isReady() const
{
    return !m_impl->options.endpoint.trimmed().isEmpty()
           && !m_impl->options.model.trimmed().isEmpty();
}

QStringList AiTranslator::translate(const QStringList& lines, const QString& source,
                                    const QString& target)
{
    QStringList result;
    result.resize(lines.size()); // rỗng = đoạn đó lỗi

    QStringList payload;
    QList<int> payloadIndex;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed().isEmpty())
            continue;
        payload.append(lines.at(i));
        payloadIndex.append(i);
    }
    if (payload.isEmpty())
        return result;

    QJsonArray texts;
    for (const QString& line : payload)
        texts.append(line);

    QJsonArray messages;
    QJsonObject systemMessage;
    systemMessage.insert(QStringLiteral("role"), QStringLiteral("system"));
    systemMessage.insert(QStringLiteral("content"),
                         buildSystemPrompt(m_impl->options.systemPrompt, source, target));
    messages.append(systemMessage);

    QJsonObject userMessage;
    userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMessage.insert(QStringLiteral("content"),
                       QStringLiteral("Dịch mảng JSON sau:\n")
                           + QString::fromUtf8(
                               QJsonDocument(texts).toJson(QJsonDocument::Compact)));
    messages.append(userMessage);

    QJsonObject root;
    root.insert(QStringLiteral("model"), m_impl->options.model);
    root.insert(QStringLiteral("messages"), messages);
    root.insert(QStringLiteral("temperature"), 0.2);

    QNetworkRequest request{QUrl(m_impl->options.endpoint.trimmed())};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("EZ-Translator"));
    if (!m_impl->options.apiKey.trimmed().isEmpty()) {
        request.setRawHeader("Authorization",
                             "Bearer " + m_impl->options.apiKey.trimmed().toUtf8());
    }

    QNetworkReply* reply = m_impl->manager.post(
        request, QJsonDocument(root).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(kTimeoutMs);
    loop.exec();

    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();

    if (networkError != QNetworkReply::NoError) {
        qWarning().noquote() << "[AI] lỗi mạng:" << networkErrorText
                             << "| body:" << QString::fromUtf8(body.left(300));
        return result;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning().noquote() << "[AI] response không phải JSON:"
                             << QString::fromUtf8(body.left(300));
        return result;
    }

    const QJsonObject object = document.object();
    const QString content = extractContent(object);
    if (content.trimmed().isEmpty()) {
        qWarning().noquote() << "[AI] response thiếu choices/message.content:"
                             << QString::fromUtf8(body.left(300));
        return result;
    }

    const QStringList translated = parseTranslations(content, payload.size());
    for (int k = 0; k < translated.size() && k < payloadIndex.size(); ++k)
        result[payloadIndex.at(k)] = translated.at(k).trimmed();

    return result;
}

} // namespace EZTranslator
