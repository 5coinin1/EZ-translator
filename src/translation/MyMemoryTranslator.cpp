#include "translation/MyMemoryTranslator.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace EZTranslator {

namespace {

constexpr int kTimeoutMs = 5000;

QString parseMyMemory(const QByteArray& data)
{
    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (!document.isObject())
        return {};
    const QJsonObject root = document.object();
    return root.value(QStringLiteral("responseData"))
        .toObject()
        .value(QStringLiteral("translatedText"))
        .toString();
}

} // namespace

struct MyMemoryTranslator::Impl
{
    QNetworkAccessManager manager;
};

MyMemoryTranslator::MyMemoryTranslator()
    : m_impl(std::make_unique<Impl>())
{
}

MyMemoryTranslator::~MyMemoryTranslator() = default;

QStringList MyMemoryTranslator::translate(const QStringList& lines, const QString& source,
                                          const QString& target)
{
    QStringList result;
    result.reserve(lines.size());

    const QString from = (source.isEmpty() || source == QLatin1String("auto"))
                             ? QStringLiteral("en")
                             : source;

    for (const QString& line : lines) {
        if (line.trimmed().isEmpty()) {
            result.append(QString());
            continue;
        }

        QUrl url(QStringLiteral("https://api.mymemory.translated.net/get"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("q"), line);
        query.addQueryItem(QStringLiteral("langpair"), from + QLatin1Char('|') + target);
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
            translated = parseMyMemory(reply->readAll());
        reply->deleteLater();

        result.append(translated);
    }

    return result;
}

} // namespace EZTranslator
