#pragma once
#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>

class AppController; // forward declare

/** Quản lý biểu tượng và menu khay hệ thống */
class TrayIconManager : public QObject
{
    Q_OBJECT
public:
    explicit TrayIconManager(QObject* parent = nullptr);

    void updateState(bool isRunning);

    /** Phím tắt đang gán cho "Bắt đầu/Dừng dịch" — hiển thị trên menu khay. */
    void setHotkeyText(const QString& keyText);

signals:
    void requestShowMainWindow();
    void requestToggleTranslation();
    void requestOpenSettings();
    void requestQuit();

private:
    void buildMenu();
    QSystemTrayIcon* m_trayIcon{nullptr};
    QMenu*           m_menu{nullptr};
    QAction*         m_statusAction{nullptr};
    QAction*         m_toggleAction{nullptr};
    QString          m_hotkeyText{QStringLiteral("Alt+G")};
    bool             m_running{false};
};
