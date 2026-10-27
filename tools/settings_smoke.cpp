// Kiểm chứng SettingsStore: lưu/nạp ĐỦ mọi trường của AppSettings + migrate từ nhóm
// "translation" của bản cũ. Chạy độc lập, dùng file INI trong thư mục tạm (không đụng
// registry thật của người dùng).
//
// Exit code: 0 = tất cả PASS, 1 = có FAIL (in rõ trường nào lệch).
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QString>

#include <cmath>
#include <cstdio>

#include "core/SettingsStore.h"
#include "core/HotkeyBindings.h"

using EZTranslator::AppSettings;

namespace {

int g_failures = 0;

void check(bool ok, const char* what, const QString& detail = QString())
{
    std::printf("%s %s%s\n", ok ? "[PASS]" : "[FAIL]", what,
                detail.isEmpty() ? "" : qPrintable(QStringLiteral("  (%1)").arg(detail)));
    if (!ok)
        ++g_failures;
}

QString iniDirectory()
{
    return QDir::tempPath() + QStringLiteral("/ez_settings_smoke");
}

void wipeStore()
{
    QDir(iniDirectory()).removeRecursively();
    QDir().mkpath(iniDirectory());
}

AppSettings makeSampleSettings()
{
    AppSettings s;
    // Mọi trường đều khác default → lệch một trường là phát hiện được ngay.
    s.startWithWindows = true;
    s.minimizeToTrayOnClose = false;
    s.checkForUpdates = false;
    s.interfaceLanguage = QStringLiteral("en");

    s.translationEngine = QStringLiteral("ai");
    s.googleApiKey = QStringLiteral("GOOGLE-KEY-123");
    s.aiEndpoint = QStringLiteral("http://localhost:11434/v1/chat/completions");
    s.aiApiKey = QStringLiteral("AI-KEY-456");
    s.aiModel = QStringLiteral("qwen2.5:7b");
    s.aiSystemPrompt = QStringLiteral("Prompt rất riêng để test lưu/đọc.");
    s.sourceLanguage = QStringLiteral("ja");
    s.targetLanguage = QStringLiteral("vi");

    s.ocrEngine = QStringLiteral("paddleocr");
    s.ocrUseGpu = false;
    s.ocrDetDevice = QStringLiteral("directml");
    s.ocrRecDevice = QStringLiteral("cpu");
    s.translateDevice = QStringLiteral("cuda");
    s.ocrBinarize = true;
    s.cacheCapacityLines = 12345;

    s.captureFps = 42;
    s.skipStaticFrames = false;
    s.useGpuAcceleration = true;
    s.overlayOpacity = 0.75;

    s.windowColor = QColor(10, 20, 30, 200);
    s.fontColor = QColor(250, 240, 230, 255);
    s.fontSize = 21;
    s.isBold = false;
    s.lineSpacing = 17;
    s.keepSourceFormatting = false;
    s.textAlignment = 2;
    s.autoClearWindow = true;
    s.excludeFromCapture = false;
    s.windowOpacity = 63;

    int hotkeyIndex = 1;
    for (const auto& binding : EZTranslator::hotkeyBindings)
        s.*(binding.value) = QStringLiteral("Ctrl+Alt+F%1").arg(hotkeyIndex++);

    return s;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("EZTeamSmoke"));
    app.setApplicationName(QStringLiteral("SettingsSmoke"));

    // File INI trong temp: không bao giờ ghi vào cấu hình thật của người dùng.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, iniDirectory());
    wipeStore();
    std::printf("== SettingsStore smoke test (INI: %s) ==\n", qPrintable(iniDirectory()));

    // ── 1. Chưa lưu gì → phải là default ────────────────────────────────────
    {
        AppSettings loaded;
        EZTranslator::SettingsStore::load(loaded);
        const AppSettings defaults;
        check(loaded.translationEngine == defaults.translationEngine, "defaults.translationEngine");
        check(loaded.captureFps == defaults.captureFps, "defaults.captureFps");
        check(loaded.keepSourceFormatting == defaults.keepSourceFormatting,
              "defaults.keepSourceFormatting");
        check(loaded.toggleTranslationHotkey == defaults.toggleTranslationHotkey,
              "defaults.toggleTranslationHotkey");
    }

    // ── 2. Lưu toàn bộ rồi nạp lại → từng trường phải khớp ─────────────────
    {
        const AppSettings original = makeSampleSettings();
        EZTranslator::SettingsStore::save(original);

        AppSettings loaded;
        EZTranslator::SettingsStore::load(loaded);

        const auto sameQt = [](const QString& a, const QString& b) { return a == b; };

        check(loaded.startWithWindows == original.startWithWindows, "startWithWindows");
        check(loaded.minimizeToTrayOnClose == original.minimizeToTrayOnClose,
              "minimizeToTrayOnClose");
        check(loaded.checkForUpdates == original.checkForUpdates, "checkForUpdates");
        check(sameQt(loaded.interfaceLanguage, original.interfaceLanguage), "interfaceLanguage");

        check(sameQt(loaded.translationEngine, original.translationEngine), "translationEngine");
        check(sameQt(loaded.googleApiKey, original.googleApiKey), "googleApiKey");
        check(sameQt(loaded.aiEndpoint, original.aiEndpoint), "aiEndpoint");
        check(sameQt(loaded.aiApiKey, original.aiApiKey), "aiApiKey");
        check(sameQt(loaded.aiModel, original.aiModel), "aiModel");
        check(sameQt(loaded.aiSystemPrompt, original.aiSystemPrompt), "aiSystemPrompt");
        check(sameQt(loaded.sourceLanguage, original.sourceLanguage), "sourceLanguage");
        check(sameQt(loaded.targetLanguage, original.targetLanguage), "targetLanguage");

        check(sameQt(loaded.ocrEngine, original.ocrEngine), "ocrEngine");
        check(loaded.ocrUseGpu == original.ocrUseGpu, "ocrUseGpu");
        check(loaded.ocrDetDevice == original.ocrDetDevice, "ocrDetDevice");
        check(loaded.ocrRecDevice == original.ocrRecDevice, "ocrRecDevice");
        check(loaded.translateDevice == original.translateDevice, "translateDevice");
        check(loaded.ocrBinarize == original.ocrBinarize, "ocrBinarize");
        check(loaded.cacheCapacityLines == original.cacheCapacityLines, "cacheCapacityLines");

        check(loaded.captureFps == original.captureFps, "captureFps");
        check(loaded.skipStaticFrames == original.skipStaticFrames, "skipStaticFrames");
        check(loaded.useGpuAcceleration == original.useGpuAcceleration, "useGpuAcceleration");
        check(std::fabs(loaded.overlayOpacity - original.overlayOpacity) < 1e-9, "overlayOpacity");

        check(loaded.windowColor == original.windowColor, "windowColor",
              QStringLiteral("%1 vs %2").arg(loaded.windowColor.name(QColor::HexArgb),
                                              original.windowColor.name(QColor::HexArgb)));
        check(loaded.fontColor == original.fontColor, "fontColor");
        check(loaded.fontSize == original.fontSize, "fontSize");
        check(loaded.isBold == original.isBold, "isBold");
        check(loaded.lineSpacing == original.lineSpacing, "lineSpacing");
        check(loaded.keepSourceFormatting == original.keepSourceFormatting,
              "keepSourceFormatting");
        check(loaded.textAlignment == original.textAlignment, "textAlignment");
        check(loaded.autoClearWindow == original.autoClearWindow, "autoClearWindow");
        check(loaded.excludeFromCapture == original.excludeFromCapture, "excludeFromCapture");
        check(loaded.windowOpacity == original.windowOpacity, "windowOpacity");

        for (const auto& binding : EZTranslator::hotkeyBindings)
            check(sameQt(loaded.*(binding.value), original.*(binding.value)),
                  binding.settingsKey);
    }

    // ── 3. Migrate từ nhóm "translation" của bản cũ ─────────────────────────
    {
        wipeStore();
        {
            QSettings legacy;
            legacy.beginGroup(QStringLiteral("translation"));
            legacy.setValue(QStringLiteral("engine"), QStringLiteral("google"));
            legacy.setValue(QStringLiteral("googleApiKey"), QStringLiteral("LEGACY-KEY"));
            legacy.setValue(QStringLiteral("aiModel"), QStringLiteral("legacy-model"));
            legacy.setValue(QStringLiteral("sourceLanguage"), QStringLiteral("zh"));
            legacy.endGroup();
            legacy.sync();
        }

        AppSettings migrated;
        EZTranslator::SettingsStore::load(migrated);

        const auto sameQt = [](const QString& a, const QString& b) { return a == b; };
        check(sameQt(migrated.translationEngine, QStringLiteral("google")), "migrate engine");
        check(sameQt(migrated.googleApiKey, QStringLiteral("LEGACY-KEY")), "migrate googleApiKey");
        check(sameQt(migrated.aiModel, QStringLiteral("legacy-model")), "migrate aiModel");
        check(sameQt(migrated.sourceLanguage, QStringLiteral("zh")), "migrate sourceLanguage");
        // Trường không có trong bản cũ vẫn giữ default.
        check(migrated.ocrEngine == AppSettings().ocrEngine, "migrate giữ default ocrEngine");
    }

    std::printf("== %s (%d lỗi) ==\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
