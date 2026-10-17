#include "translation/GoogleTranslator.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

namespace EZTranslator {

namespace {

constexpr int kTimeoutMs = 5000;

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

} // namespace

struct GoogleTranslator::Impl
{
    QNetworkAccessManager manager;
};

GoogleTranslator::GoogleTranslator()
    : m_impl(std::make_unique<Impl>())
{
}

GoogleTranslator::~GoogleTranslator() = default;

QStringList GoogleTranslator::translate(const QStringList& lines, const QString& source,
                                        const QString& target)
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
        query.addQueryItem(QStringLiteral("sl"), source.isEmpty() ? QStringLiteral("auto") : source);
        query.addQueryItem(QStringLiteral("tl"), target);
        query.addQueryItem(QStringLiteral("dt"), QStringLiteral("t"));
        query.addQueryItem(QStringLiteral("q"), line);
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("EZ-Translator"));

        QNetworkReply* reply = m_impl->manager.get(request);
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

        QString translated;
        if (reply->error() == QNetworkReply::NoError)
            translated = parseGtxResponse(reply->readAll());
        reply->deleteLater();

        result.append(translated);
    }

    return result;
}

} // namespace EZTranslator
