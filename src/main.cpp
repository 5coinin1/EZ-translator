#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <QWidget>
#include <QPixmap>
#include "ui/theme/StyleTheme.h"
#include "app/AppController.h"

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

    app.setApplicationName("EZ-Translator");
    app.setApplicationDisplayName("EZ-Translator");
    app.setOrganizationName("EZTeam");
    app.setOrganizationDomain("eztranslator.local");

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
