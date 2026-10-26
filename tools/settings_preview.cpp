// Render các trang của SettingsDialog ra PNG (chạy offscreen) — soi giao diện cài đặt mà
// không cần mở app thật. Dùng cài đặt THẬT đang lưu để thấy đúng trạng thái người dùng.
//
//   set QT_QPA_PLATFORM=offscreen
//   settings_preview.exe [thư-mục-xuất] [rộng] [cao]
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QListWidget>
#include <QPixmap>
#include <QStackedWidget>

#include <cstdio>

#include "core/SettingsStore.h"
#include "core/Types.h"
#include "ui/SettingsDialog.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("EZTeam"));
    app.setApplicationName(QStringLiteral("EZ-Translator"));

    EZTranslator::AppSettings settings;
    EZTranslator::SettingsStore::load(settings);

    SettingsDialog dialog;
    dialog.setSettings(settings);
    const int width = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 880;
    const int height = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 680;
    dialog.resize(width, height);
    // Đặt ngoài màn hình: render được đủ nét với font thật mà không nháy cửa sổ trước mắt
    // người dùng (offscreen platform không có font hệ thống nên chữ ra ô vuông).
    dialog.move(-4000, -4000);
    dialog.show();
    QApplication::processEvents();

    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                    : QDir::currentPath() + QStringLiteral("/build/settings_preview");
    QDir().mkpath(outDir);

    auto* sidebar = dialog.findChild<QListWidget*>();
    if (!sidebar) {
        std::fprintf(stderr, "không tìm thấy sidebar\n");
        return 2;
    }

    for (int row = 0; row < sidebar->count(); ++row) {
        sidebar->setCurrentRow(row);
        QApplication::processEvents();

        QPixmap shot(dialog.size());
        shot.fill(Qt::transparent);
        dialog.render(&shot);

        QString name = sidebar->item(row)->text();
        name.replace(QLatin1Char(' '), QLatin1Char('_'));
        const QString path = QStringLiteral("%1/%2_%3.png").arg(outDir).arg(row).arg(name);
        if (!shot.save(path)) {
            std::fprintf(stderr, "không ghi được %s\n", qPrintable(path));
            return 3;
        }
        std::printf("saved %s\n", qPrintable(path));
    }
    return 0;
}
