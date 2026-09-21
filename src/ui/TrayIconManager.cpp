#include "TrayIconManager.h"
#include "ui/theme/StyleTheme.h"

#include <QApplication>
#include <QIcon>
#include <QPixmap>
#include <QPainter>

TrayIconManager::TrayIconManager(QObject* parent)
    : QObject(parent)
{
    m_trayIcon = new QSystemTrayIcon(this);

    // Tạo icon mặc định cho Tray (hình logo tròn màu Primary)
    QPixmap pix(32, 32);
    pix.fill(Qt::transparent);
    {
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(StyleTheme::ColorPrimary));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(2, 2, 28, 28, 8, 8);

        p.setPen(Qt::white);
        QFont f = p.font();
        f.setBold(true);
        f.setPointSize(12);
        p.setFont(f);
        p.drawText(pix.rect(), Qt::AlignCenter, "EZ");
    }
    m_trayIcon->setIcon(QIcon(pix));
    m_trayIcon->setToolTip(QString::fromUtf8("EZ-Translator – Sẵn sàng"));

    buildMenu();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) {
            emit requestShowMainWindow();
        }
    });

    m_trayIcon->show();
}

void TrayIconManager::buildMenu()
{
    m_menu = new QMenu();
    m_menu->setStyleSheet(QString(R"(
        QMenu {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: 8px;
            padding: 6px;
        }
        QMenu::item {
            padding: 8px 20px 8px 12px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background-color: %4;
            color: #FFFFFF;
        }
        QMenu::separator {
            height: 1px;
            background-color: %3;
            margin: 4px 6px;
        }
    )")
    .arg(StyleTheme::ColorSurface)
    .arg(StyleTheme::ColorTextPrimary)
    .arg(StyleTheme::ColorBorder)
    .arg(StyleTheme::ColorPrimary));

    // Trạng thái hiện tại (disabled)
    m_statusAction = m_menu->addAction(QString::fromUtf8("● Trạng thái: Dừng"));
    m_statusAction->setEnabled(false);

    m_menu->addSeparator();

    // Mở cửa sổ chính
    auto* showAction = m_menu->addAction(QString::fromUtf8("🖥 Mở EZ-Translator"));
    connect(showAction, &QAction::triggered, this, &TrayIconManager::requestShowMainWindow);

    // Bắt đầu / Tạm dừng dịch
    m_toggleAction = m_menu->addAction(QString::fromUtf8("▶ Bắt đầu dịch (F9)"));
    connect(m_toggleAction, &QAction::triggered, this, &TrayIconManager::requestToggleTranslation);

    // Cài đặt
    auto* settingsAction = m_menu->addAction(QString::fromUtf8("⚙ Cài đặt..."));
    connect(settingsAction, &QAction::triggered, this, &TrayIconManager::requestOpenSettings);

    m_menu->addSeparator();

    // Thoát
    auto* quitAction = m_menu->addAction(QString::fromUtf8("❌ Thoát"));
    connect(quitAction, &QAction::triggered, this, &TrayIconManager::requestQuit);

    m_trayIcon->setContextMenu(m_menu);
}

void TrayIconManager::updateState(bool isRunning)
{
    m_running = isRunning;
    if (m_running) {
        m_statusAction->setText(QString::fromUtf8("● Trạng thái: Đang dịch"));
        m_toggleAction->setText(QString::fromUtf8("⏹ Dừng dịch (F9)"));
        m_trayIcon->setToolTip(QString::fromUtf8("EZ-Translator – Đang dịch"));
    } else {
        m_statusAction->setText(QString::fromUtf8("● Trạng thái: Dừng"));
        m_toggleAction->setText(QString::fromUtf8("▶ Bắt đầu dịch (F9)"));
        m_trayIcon->setToolTip(QString::fromUtf8("EZ-Translator – Sẵn sàng"));
    }
}
