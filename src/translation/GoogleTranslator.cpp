#include "translation/GoogleTranslator.h"

#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace EZTranslator {

namespace {

constexpr int kFreeTimeoutMs = 4000; ///< chặn 'kẹt luồng' khi mạng treo
constexpr int kApiTimeoutMs = 10000;

/** Ghép các đoạn dịch trong response gtx thành một chuỗi. */
QString parseGtxResponse(const QByteArray& data)
{
    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (!document.isArray())
        return {};

    const QJsonArray root = document.array();
    if (root.isEmpty() || !root.at(0).isArray())
        return {};

    QString text;
    const QJsonArray segments = root.at(0).toArray();
    for (const QJsonValue& segment : segments) {
        if (!segment.isArray())
            continue;
        const QJsonArray parts = segment.toArray();
        if (!parts.isEmpty() && parts.at(0).isString())
            text += parts.at(0).toString();
    }
    return text;
}

/** Google trả translatedText dạng HTML-escaped; đổi về chữ thường. */
QString decodeEntities(QString text)
{
    static const QRegularExpression numeric(QStringLiteral("&#(x?)([0-9A-Fa-f]+);"));

    QString out;
    out.reserve(text.size());
    int last = 0;
    QRegularExpressionMatchIterator it = numeric.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        out += text.mid(last, match.capturedStart() - last);

        bool ok = false;
        const uint code = match.captured(1) == QLatin1String("x")
                              ? match.captured(2).toUInt(&ok, 16)
                              : match.captured(2).toUInt(&ok, 10);
        if (ok && code > 0 && code <= 0x10FFFF) {
            const char32_t cp = char32_t(code);
            out += QString::fromUcs4(&cp, 1);
        } else {
            out += match.captured(0);
        }
        last = match.capturedEnd();
    }
    out += text.mid(last);

    out.replace(QLatin1String("&quot;"), QStringLiteral("\""));
    out.replace(QLatin1String("&apos;"), QStringLiteral("'"));
    out.replace(QLatin1String("&lt;"), QStringLiteral("<"));
    out.replace(QLatin1String("&gt;"), QStringLiteral(">"));
    out.replace(QLatin1String("&nbsp;"), QStringLiteral(" "));
    out.replace(QLatin1String("&amp;"), QStringLiteral("&")); // sau cùng để không decode lặp
    return out;
}

/** Chờ reply xong, trả toàn bộ body (rỗng nếu lỗi/timeout). */
QByteArray waitForReply(QNetworkReply* reply, int timeoutMs, const QString& logTag)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(timeoutMs);
    loop.exec();

    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        qWarning().noquote() << logTag << "lỗi:" << reply->errorString()
                             << "| body:" << QString::fromUtf8(body.left(300));
    }
    reply->deleteLater();
    return body;
}

/** Endpoint miễn phí gtx: dịch từng dòng. */
QStringList translateFree(QNetworkAccessManager& manager, const QStringList& lines,
                          const QString& source, const QString& target)
{
    QStringList result;
    result.reserve(lines.size());

    for (const QString& line : lines) {
        if (line.trimmed().isEmpty()) {
            result.append(QString());
            continue;
        }

        QUrl url(QStringLiteral("https://translate.googleapis.com/translate_a/single"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("client"), QStringLiteral("gtx"));
        query.addQueryItem(QStringLiteral("sl"),
                           source.isEmpty() ? QStringLiteral("auto") : source);
        query.addQueryItem(QStringLiteral("tl"), target);
        query.addQueryItem(QStringLiteral("dt"), QStringLiteral("t"));
        query.addQueryItem(QStringLiteral("q"), line);
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("EZ-Translator"));

        QNetworkReply* reply = manager.get(request);
        const QByteArray body = waitForReply(reply, kFreeTimeoutMs, QStringLiteral("[Google] gtx"));
        result.append(parseGtxResponse(body));
    }

    return result;
}

/** Cloud Translation API v2: batch các dòng trong một request. */
QStringList translateOfficial(QNetworkAccessManager& manager, const QString& apiKey,
                              const QStringList& lines, const QString& source,
                              const QString& target)
{
    QStringList result;
    result.resize(lines.size()); // rỗng = dòng đó lỗi

    QJsonArray texts;
    QList<int> indices;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed().isEmpty())
            continue;
        texts.append(lines.at(i));
        indices.append(i);
    }
    if (texts.isEmpty())
        return result;

    QJsonObject root;
    root.insert(QStringLiteral("q"), texts);
    root.insert(QStringLiteral("target"), target);
    root.insert(QStringLiteral("format"), QStringLiteral("text"));
    if (!source.isEmpty() && source != QLatin1String("auto"))
        root.insert(QStringLiteral("source"), source);

    QUrl url(QStringLiteral("https://translation.googleapis.com/language/translate/v2"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), apiKey);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("EZ-Translator"));

    QNetworkReply* reply = manager.post(request,
                                        QJsonDocument(root).toJson(QJsonDocument::Compact));
    const QByteArray body = waitForReply(reply, kApiTimeoutMs, QStringLiteral("[Google] API"));
    if (body.isEmpty())
        return result;

    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject())
        return result;

    const QJsonArray translations = document.object()
                                        .value(QStringLiteral("data"))
                                        .toObject()
                                        .value(QStringLiteral("translations"))
                                        .toArray();
    for (int k = 0; k < translations.size() && k < indices.size(); ++k) {
        const QString translated =
            translations.at(k).toObject().value(QStringLiteral("translatedText")).toString();
        result[indices.at(k)] = decodeEntities(translated);
    }
    return result;
}

} // namespace

struct GoogleTranslator::Impl
{
    QString apiKey;
    QNetworkAccessManager manager;
};

GoogleTranslator::GoogleTranslator()
    : m_impl(std::make_unique<Impl>())
{
}

GoogleTranslator::~GoogleTranslator() = default;

void GoogleTranslator::setApiKey(const QString& apiKey)
{
    m_impl->apiKey = apiKey.trimmed();
}

QStringList GoogleTranslator::translate(const QStringList& lines, const QString& source,
                                        const QString& target)
{
    if (!m_impl->apiKey.isEmpty()) {
        QStringList result =
            translateOfficial(m_impl->manager, m_impl->apiKey, lines, source, target);

        const bool any = std::any_of(result.cbegin(), result.cend(),
                                     [](const QString& text) { return !text.isEmpty(); });
        if (any)
            return result;

        // Key sai/hết hạn/quota → vẫn cố dịch bằng endpoint miễn phí.
        qWarning() << "[Google] API chính thức không có kết quả → thử endpoint miễn phí";
    }

    return translateFree(m_impl->manager, lines, source, target);
}

} // namespace EZTranslator
