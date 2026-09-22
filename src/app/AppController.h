#pragma once

#include <QObject>
#include <QPointer>
#include "core/Types.h"
#include "capture/IWindowCapture.h"

class MainWindow;
class RegionEditorWindow;
class RegionSnipperOverlay;
class RegionHighlightOverlay;
class SettingsDialog;
class MiniFloatBar;
class TrayIconManager;

namespace EZTranslator { class GdiWindowCapture; }

/**
 * AppController – Điều phối luồng và trạng thái giữa các màn hình UI.
 *
 * Đảm bảo các Window không gọi trực tiếp lẫn nhau. Mọi tương tác chuyển màn hình,
 * chuyển trạng thái đều đi qua AppController bằng Qt Signals & Slots.
 */
class AppController : public QObject
{
    Q_OBJECT
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    /** Khởi động ứng dụng, hiển thị MainWindow và khay hệ thống */
    void start();

public slots:
    void onStartTranslation();
    void onStopTranslation();
    void onToggleTranslation();

    void onOpenRegionEditor();
    void onRegionEditorSaved(const QList<EZTranslator::TranslationRegion>& regions);
    void onRegionEditorCancelled();

    void onRegionSnapped(const EZTranslator::NormalizedRect& rect, const QRect& screenRect);
    void onRegionSnapCancelled();
    void onShowRegion();

    void onOpenSettings();
    void onSettingsSaved(const EZTranslator::AppSettings& settings);

    void onShowMainWindow();
    void onQuit();

    /** Goi khi user chon cua so moi tu dropdown */
    void onTargetWindowSelected(quintptr handle, const QString& title, const QString& processName);

    /** Nhan frame tu capture va chuyen len MainWindow preview */
    void onCaptureFrame(const EZTranslator::CapturedFrame& frame);

    /** Nhan loi tu capture va hien thi warning */
    void onCaptureError(EZTranslator::CaptureError error, const QString& detail);

private:
    void initConnections();

    MainWindow*               m_mainWindow{nullptr};
    RegionEditorWindow*       m_regionEditor{nullptr};
    RegionSnipperOverlay*     m_snipperOverlay{nullptr};
    RegionHighlightOverlay*   m_highlightOverlay{nullptr};
    SettingsDialog*           m_settingsDialog{nullptr};
    MiniFloatBar*             m_miniFloatBar{nullptr};
    TrayIconManager*          m_trayManager{nullptr};
    EZTranslator::GdiWindowCapture* m_capture{nullptr};

    EZTranslator::TranslationState m_state{EZTranslator::TranslationState::Idle};
    EZTranslator::AppSettings m_settings;
    QList<EZTranslator::TranslationRegion> m_regions;
    QRect m_currentScreenRect;
};
