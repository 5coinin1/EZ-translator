#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <QWidget>
#include <QPixmap>
#include "ui/theme/StyleTheme.h"
#include "app/AppController.h"
#include "core/CudaOcrPack.h"
#include "core/SettingsStore.h"

void logToFile(QtMsgType, const QMessageLogContext&, const QString& msg)
{
    QFile f("ez_debug.log");
    if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ") << msg << "\n";
        ts.flush();
        f.flush();
    }
}

int main(int argc, char* argv[])
{
    qInstallMessageHandler(logToFile);
    qDebug() << "Entering main()";

    QApplication app(argc, argv);
    qDebug() << "QApplication created";
    qInfo().noquote() << "[BUILD] overlay-scroll-layout-v2 executable="
                      << QCoreApplication::applicationFilePath();

    app.setApplicationName("EZ-Translator");
    app.setApplicationDisplayName("EZ-Translator");
    app.setOrganizationName("EZTeam");
    app.setOrganizationDomain("eztranslator.local");

    if (!qEnvironmentVariableIsSet("EZ_CUDA_PROFILE")
        && !app.arguments().contains(QStringLiteral("--render-preview"))) {
        EZTranslator::AppSettings startupSettings;
        EZTranslator::SettingsStore::load(startupSettings);
        if (EZTranslator::CudaOcrPack::requested(startupSettings)
            && EZTranslator::CudaOcrPack::isInstalled()
            && !EZTranslator::CudaOcrPack::isProfileProcess()) {
            const QString baseDir = QCoreApplication::applicationDirPath();
            const QString profileDir = EZTranslator::CudaOcrPack::profileDir();
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            QStringList paths = {profileDir, baseDir};
            QString modelsDir;
            QFile envFile(EZTranslator::CudaOcrPack::envPath());
            if (envFile.open(QIODevice::ReadOnly)) {
                const QStringList lines =
                    QString::fromUtf8(envFile.readAll()).split(QLatin1Char('\n'));
                for (const QString& raw : lines) {
                    const QString line = raw.trimmed();
                    if (line.startsWith(QStringLiteral("MODELS=")))
                        modelsDir = line.mid(7);
                    else if (!line.isEmpty() && QDir(line).exists())
                        paths.prepend(line);
                }
            }
            env.insert(QStringLiteral("PATH"),
                       paths.join(QLatin1Char(';')) + QLatin1Char(';')
                           + env.value(QStringLiteral("PATH")));
            env.insert(QStringLiteral("EZ_CUDA_PROFILE"), QStringLiteral("1"));
            env.insert(QStringLiteral("QT_PLUGIN_PATH"), baseDir);
            env.insert(QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH"),
                       QDir(baseDir).filePath(QStringLiteral("platforms")));
            if (!modelsDir.isEmpty())
                env.insert(QStringLiteral("EZ_MODELS_DIR"), modelsDir);
            qInfo().noquote() << "[LAUNCH] CUDA OCR profile =" << profileDir;
            QProcess launcher;
            launcher.setProgram(EZTranslator::CudaOcrPack::profileExe());
            launcher.setArguments(QStringList());
            launcher.setWorkingDirectory(profileDir);
            launcher.setProcessEnvironment(env);
            if (launcher.startDetached())
                return 0;
            qWarning() << "[LAUNCH] Không chạy được CUDA OCR profile; dùng DirectML.";
        }
    }

    // Áp dụng font và stylesheet tập trung
    QFont defaultFont(StyleTheme::FontFamily, StyleTheme::FontSizeBody);
    app.setFont(defaultFont);
    app.setStyleSheet(StyleTheme::globalStylesheet());
    qDebug() << "Styles applied";

    // Khởi tạo bộ điều phối ứng dụng
    AppController controller;
    qDebug() << "AppController instantiated";
    controller.start();
    qDebug() << "controller.start() called, entering app.exec()";

    if (app.arguments().contains("--render-preview")) {
        QTimer::singleShot(600, [&]() {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (w->isVisible() && w->width() > 200) {
                    QPixmap pix(w->size());
                    pix.fill(Qt::transparent);
                    w->render(&pix);
                    pix.save("C:/Users/tucoi/.gemini/antigravity-ide/brain/5e560025-e0f9-4260-af23-49a43fdde2a8/ez_preview.png");
                    qDebug() << "Saved preview to ez_preview.png";
                    break;
                }
            }
            app.quit();
        });
    }

    int ret = app.exec();
    qDebug() << "app.exec() returned:" << ret;
    return ret;
}
