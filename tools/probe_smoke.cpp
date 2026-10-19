// Chạy thử TranslationProbe (nút "Kiểm tra engine" trong Cài đặt) từ dòng lệnh.
//
//   probe_smoke.exe                                   # engine mặc định: auto
//   probe_smoke.exe google                            # kiểm tra Google
//   probe_smoke.exe offline EZ_SRC=zh EZ_TGT=vi       # kiểm tra offline
//   EZ_SRC=ja EZ_TGT=vi probe_smoke.exe ai            # kiểm tra AI (cần endpoint/key trong EZ_AI_*)
//
// Exit code: 0 = engine trả lời được (PASS), 1 = FAIL.
#include <QCoreApplication>
#include <QDebug>
#include <QString>

#include <cstdio>

#include "translation/TranslationProbe.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    EZTranslator::TranslationConfig config;
    config.engine = argc > 1 ? QString::fromLocal8Bit(argv[1])
                             : qEnvironmentVariable("EZ_ENGINE", QStringLiteral("auto"));
    config.source = qEnvironmentVariable("EZ_SRC", QStringLiteral("en"));
    config.target = qEnvironmentVariable("EZ_TGT", QStringLiteral("vi"));
    config.googleApiKey = qEnvironmentVariable("EZ_GOOGLE_KEY");
    config.aiEndpoint = qEnvironmentVariable("EZ_AI_ENDPOINT",
                                             QStringLiteral("https://api.openai.com/v1/chat/completions"));
    config.aiApiKey = qEnvironmentVariable("EZ_AI_KEY");
    config.aiModel = qEnvironmentVariable("EZ_AI_MODEL", QStringLiteral("gpt-4o-mini"));

    std::printf("== TranslationProbe: engine=%s %s->%s ==\n", qPrintable(config.engine),
                qPrintable(config.source), qPrintable(config.target));

    const EZTranslator::TranslationProbe::Result result =
        EZTranslator::TranslationProbe::run(config);

    std::printf("%s engine=%s\n%s\n", result.ok ? "[PASS]" : "[FAIL]",
                qPrintable(result.engine), qPrintable(result.detail));
    return result.ok ? 0 : 1;
}
