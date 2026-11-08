#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace EZTranslator {
namespace ModelList {

/**
 * @brief Suy ra URL liet ke model tu endpoint chat completions.
 *
 * OpenAI-compatible chuan dung `GET {base}/models`. Endpoint trong app la URL
 * day du toi `/chat/completions`, nen phai bo phan sau truoc khi ghep.
 * Tra ve URL chinh; `fallbackUrl` (neu co) la phuong an thu hai cho cac
 * provider dat base khac `/v1` (vi du `https://api.deepseek.com`).
 */
inline QUrl modelsUrl(const QString& endpointText, QString* fallbackUrl = nullptr)
{
    if (fallbackUrl)
        fallbackUrl->clear();

    const QString trimmed = endpointText.trimmed();
    if (trimmed.isEmpty())
        return {};

    const QUrl endpoint(trimmed);
    if (!endpoint.isValid() || endpoint.scheme().isEmpty() || endpoint.host().isEmpty())
        return {};

    QString path = endpoint.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);

    const QString chatSuffix = QStringLiteral("/chat/completions");
    const QString completionsSuffix = QStringLiteral("/completions");
    if (path.endsWith(chatSuffix))
        path.chop(chatSuffix.size());
    else if (path.endsWith(completionsSuffix))
        path.chop(completionsSuffix.size());

    QUrl base = endpoint;
    base.setQuery(QString());
    base.setFragment(QString());

    const auto withPath = [&base](const QString& value) {
        QUrl url = base;
        url.setPath(value);
        return url;
    };

    if (path.isEmpty()) {
        if (fallbackUrl) {
            const QUrl fallback = withPath(QStringLiteral("/models"));
            *fallbackUrl = fallback.toString();
        }
        return withPath(QStringLiteral("/v1/models"));
    }

    if (fallbackUrl && !path.endsWith(QLatin1String("/v1"))) {
        const QUrl fallback = withPath(path + QStringLiteral("/v1/models"));
        *fallbackUrl = fallback.toString();
    }
    return withPath(path + QStringLiteral("/models"));
}

/** Doc `data[].id` (OpenAI) hoac `models[].name` (Ollama native). */
inline QStringList parseModels(const QByteArray& body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject())
        return {};

    const QJsonObject root = document.object();
    QStringList models;
    const auto append = [&models](const QJsonValue& value) {
        if (value.isString()) {
            const QString id = value.toString();
            if (!id.isEmpty())
                models.append(id);
            return;
        }
        if (!value.isObject())
            return;
        const QJsonObject object = value.toObject();
        QString id = object.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            id = object.value(QStringLiteral("name")).toString();
        if (!id.isEmpty())
            models.append(id);
    };

    for (const QJsonValue& value : root.value(QStringLiteral("data")).toArray())
        append(value);
    if (models.isEmpty()) {
        for (const QJsonValue& value : root.value(QStringLiteral("models")).toArray())
            append(value);
    }

    models.removeDuplicates();
    return models;
}

/** Loai model khong dung de dich (embedding, tts, vision-only, legacy...). */
inline bool isChatModel(const QString& id)
{
    const QString lower = id.toLower();
    static const QStringList unsupported = {
        QStringLiteral("embedding"), QStringLiteral("-embed"), QStringLiteral("embed-"),
        QStringLiteral("tts"),       QStringLiteral("whisper"), QStringLiteral("dall-e"),
        QStringLiteral("moderation"), QStringLiteral("image"),  QStringLiteral("audio"),
        QStringLiteral("realtime"),  QStringLiteral("transcribe"),
        QStringLiteral("rerank"),    QStringLiteral("davinci"), QStringLiteral("babbage"),
        QStringLiteral("curie"),
    };
    for (const QString& token : unsupported) {
        if (lower.contains(token))
            return false;
    }
    return !lower.startsWith(QLatin1String("ada"));
}

inline QStringList chatModels(const QStringList& models)
{
    QStringList out;
    out.reserve(models.size());
    for (const QString& id : models) {
        if (isChatModel(id))
            out.append(id);
    }
    return out;
}

} // namespace ModelList
} // namespace EZTranslator
