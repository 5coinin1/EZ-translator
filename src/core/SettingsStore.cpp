#include "core/SettingsStore.h"

#include <QColor>
#include <QSettings>

namespace EZTranslator::SettingsStore {

namespace {

constexpr auto kGroup = "settings/v1";

/** Chuỗi rỗng (giá trị ghi lỗi từ bản cũ) không được ghi đè mặc định. */
QString nonEmpty(const QString& value, const QString& fallback)
{
    return value.isEmpty() ? fallback : value;
}

QString colorTo(const QColor& color)
{
    return color.isValid() ? color.name(QColor::HexArgb) : QString();
}

QColor colorFrom(const QString& value, const QColor& fallback)
{
    if (value.isEmpty())
        return fallback;
    const QColor color(value);
    return color.isValid() ? color : fallback;
}

/**
 * Đọc một khóa, có fallback về nhóm "translation" của bản CŨ khi khóa mới chưa
 * tồn tại (migrate một lần: lần Lưu kế tiếp sẽ ghi sang nhóm mới).
 */
QVariant valueWithLegacy(const QSettings& modern, const char* key,
                         const QString& legacyGroup, const char* legacyKey,
                         const QVariant& fallback)
{
    if (modern.contains(QLatin1String(key)))
        return modern.value(QLatin1String(key), fallback);

    QSettings store;
    store.beginGroup(legacyGroup);
    const QVariant legacy = store.value(QLatin1String(legacyKey));
    store.endGroup();
    return legacy.isValid() ? legacy : fallback;
}

} // namespace

void load(AppSettings& settings)
{
    QSettings store;
    store.beginGroup(QLatin1String(kGroup));

    const auto str = [&](const char* key, const QString& fallback,
                         bool protectEmpty = true) {
        const QString value = store.value(QLatin1String(key), fallback).toString();
        return protectEmpty ? nonEmpty(value, fallback) : value;
    };

    // ── Chung ────────────────────────────────────────────────────────────────
    settings.startWithWindows =
        store.value("startWithWindows", settings.startWithWindows).toBool();
    settings.minimizeToTrayOnClose =
        store.value("minimizeToTrayOnClose", settings.minimizeToTrayOnClose).toBool();
    settings.checkForUpdates =
        store.value("checkForUpdates", settings.checkForUpdates).toBool();
    settings.interfaceLanguage = str("interfaceLanguage", settings.interfaceLanguage);

    // ── Dịch thuật (migrate từ nhóm "translation" của bản cũ) ────────────────
    settings.translationEngine = nonEmpty(
        valueWithLegacy(store, "translationEngine", "translation", "engine",
                        settings.translationEngine).toString(),
        settings.translationEngine);
    settings.googleApiKey =
        valueWithLegacy(store, "googleApiKey", "translation", "googleApiKey",
                        settings.googleApiKey).toString();
    settings.aiEndpoint =
        valueWithLegacy(store, "aiEndpoint", "translation", "aiEndpoint",
                        settings.aiEndpoint).toString();
    settings.aiApiKey = valueWithLegacy(store, "aiApiKey", "translation", "aiApiKey",
                                        settings.aiApiKey).toString();
    settings.aiModel = valueWithLegacy(store, "aiModel", "translation", "aiModel",
                                       settings.aiModel).toString();
    settings.aiSystemPrompt =
        valueWithLegacy(store, "aiSystemPrompt", "translation", "aiSystemPrompt",
                        settings.aiSystemPrompt).toString();
    settings.sourceLanguage = nonEmpty(
        valueWithLegacy(store, "sourceLanguage", "translation", "sourceLanguage",
                        settings.sourceLanguage).toString(),
        settings.sourceLanguage);
    settings.targetLanguage = nonEmpty(
        valueWithLegacy(store, "targetLanguage", "translation", "targetLanguage",
                        settings.targetLanguage).toString(),
        settings.targetLanguage);

    // ── OCR ──────────────────────────────────────────────────────────────────
    settings.ocrEngine = str("ocrEngine", settings.ocrEngine);
    settings.ocrUseGpu = store.value("ocrUseGpu", settings.ocrUseGpu).toBool();
    settings.ocrBinarize = store.value("ocrBinarize", settings.ocrBinarize).toBool();
    settings.gameDialogMode = store.value("gameDialogMode", settings.gameDialogMode).toBool();
    settings.cacheCapacityLines =
        store.value("cacheCapacityLines", settings.cacheCapacityLines).toInt();

    // ── Hiển thị & Hiệu suất ────────────────────────────────────────────────
    settings.captureFps = store.value("captureFps", settings.captureFps).toInt();
    settings.skipStaticFrames =
        store.value("skipStaticFrames", settings.skipStaticFrames).toBool();
    settings.useGpuAcceleration =
        store.value("useGpuAcceleration", settings.useGpuAcceleration).toBool();
    settings.overlayOpacity = store.value("overlayOpacity", settings.overlayOpacity).toDouble();

    // ── Giao diện ───────────────────────────────────────────────────────────
    settings.windowColor =
        colorFrom(store.value("windowColor").toString(), settings.windowColor);
    settings.fontColor = colorFrom(store.value("fontColor").toString(), settings.fontColor);
    settings.fontFamily =
        nonEmpty(store.value("fontFamily", settings.fontFamily).toString(), settings.fontFamily);
    settings.textWindowStyle =
        store.value("textWindowStyle", settings.textWindowStyle).toInt();
    settings.textWindowShowSource =
        store.value("textWindowShowSource", settings.textWindowShowSource).toBool();
    settings.fontSize = store.value("fontSize", settings.fontSize).toInt();
    settings.isBold = store.value("isBold", settings.isBold).toBool();
    settings.lineSpacing = store.value("lineSpacing", settings.lineSpacing).toInt();
    settings.keepSourceFormatting =
        store.value("keepSourceFormatting", settings.keepSourceFormatting).toBool();
    settings.novelMode = store.value("novelMode", settings.novelMode).toBool();
    settings.textAlignment = store.value("textAlignment", settings.textAlignment).toInt();
    settings.autoClearWindow =
        store.value("autoClearWindow", settings.autoClearWindow).toBool();
    settings.excludeFromCapture =
        store.value("excludeFromCapture", settings.excludeFromCapture).toBool();
    settings.windowOpacity = store.value("windowOpacity", settings.windowOpacity).toInt();

    // ── Phím tắt ────────────────────────────────────────────────────────────
    settings.toggleTranslationHotkey =
        str("toggleTranslationHotkey", settings.toggleTranslationHotkey);
    settings.selectRegionHotkey =
        str("selectRegionHotkey", settings.selectRegionHotkey, /*protectEmpty=*/false);
    settings.showRegionHotkey =
        str("showRegionHotkey", settings.showRegionHotkey, /*protectEmpty=*/false);
    settings.openSettingsHotkey =
        str("openSettingsHotkey", settings.openSettingsHotkey, /*protectEmpty=*/false);

    store.endGroup();
}

void save(const AppSettings& settings)
{
    QSettings store;
    store.beginGroup(QLatin1String(kGroup));

    // ── Chung ────────────────────────────────────────────────────────────────
    store.setValue("startWithWindows", settings.startWithWindows);
    store.setValue("minimizeToTrayOnClose", settings.minimizeToTrayOnClose);
    store.setValue("checkForUpdates", settings.checkForUpdates);
    store.setValue("interfaceLanguage", settings.interfaceLanguage);

    // ── Dịch thuật ──────────────────────────────────────────────────────────
    store.setValue("translationEngine", settings.translationEngine);
    store.setValue("googleApiKey", settings.googleApiKey);
    store.setValue("aiEndpoint", settings.aiEndpoint);
    store.setValue("aiApiKey", settings.aiApiKey);
    store.setValue("aiModel", settings.aiModel);
    store.setValue("aiSystemPrompt", settings.aiSystemPrompt);
    store.setValue("sourceLanguage", settings.sourceLanguage);
    store.setValue("targetLanguage", settings.targetLanguage);

    // ── OCR ──────────────────────────────────────────────────────────────────
    store.setValue("ocrEngine", settings.ocrEngine);
    store.setValue("ocrUseGpu", settings.ocrUseGpu);
    store.setValue("ocrBinarize", settings.ocrBinarize);
    store.setValue("gameDialogMode", settings.gameDialogMode);
    store.setValue("cacheCapacityLines", settings.cacheCapacityLines);

    // ── Hiển thị & Hiệu suất ────────────────────────────────────────────────
    store.setValue("captureFps", settings.captureFps);
    store.setValue("skipStaticFrames", settings.skipStaticFrames);
    store.setValue("useGpuAcceleration", settings.useGpuAcceleration);
    store.setValue("overlayOpacity", settings.overlayOpacity);

    // ── Giao diện ───────────────────────────────────────────────────────────
    store.setValue("windowColor", colorTo(settings.windowColor));
    store.setValue("fontColor", colorTo(settings.fontColor));
    store.setValue("fontFamily", settings.fontFamily);
    store.setValue("textWindowStyle", settings.textWindowStyle);
    store.setValue("textWindowShowSource", settings.textWindowShowSource);
    store.setValue("fontSize", settings.fontSize);
    store.setValue("isBold", settings.isBold);
    store.setValue("lineSpacing", settings.lineSpacing);
    store.setValue("keepSourceFormatting", settings.keepSourceFormatting);
    store.setValue("novelMode", settings.novelMode);
    store.setValue("textAlignment", settings.textAlignment);
    store.setValue("autoClearWindow", settings.autoClearWindow);
    store.setValue("excludeFromCapture", settings.excludeFromCapture);
    store.setValue("windowOpacity", settings.windowOpacity);

    // ── Phím tắt ────────────────────────────────────────────────────────────
    store.setValue("toggleTranslationHotkey", settings.toggleTranslationHotkey);
    store.setValue("selectRegionHotkey", settings.selectRegionHotkey);
    store.setValue("showRegionHotkey", settings.showRegionHotkey);
    store.setValue("openSettingsHotkey", settings.openSettingsHotkey);

    store.endGroup();
    store.sync();
}

} // namespace EZTranslator::SettingsStore
