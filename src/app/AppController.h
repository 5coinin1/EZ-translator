#pragma once

#include <QObject>
#include <QPointer>
#include <QHash>
#include "core/Types.h"
#include "capture/IWindowCapture.h"
#include "region/RegionManager.h"
#include "detection/ChangeDetector.h"
#include "ocr/OcrEngine.h"

class MainWindow;
class RegionEditorWindow;
class RegionSnipperOverlay;
class RegionHighlightOverlay;
class SettingsDialog;
class MiniFloatBar;
class TrayIconManager;

namespace EZTranslator {
    class GdiWindowCapture;
    class WgcWindowCapture;
    class RoiPreviewDialog;
    class TextOverlay;
}

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
    void onPreviewRoiRequested();
    void onPreviewClosed();

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

    /** Chọn backend capture: ưu tiên WGC, fallback GDI. */
    bool startCapture(quintptr windowHandle);

    /** Nạp model OCR một lần (lazy). Trả false nếu không nạp được. */
    bool ensureOcrLoaded();

    /** Vùng client của cửa sổ đích trên màn hình (toạ độ logical của Qt). */
    [[nodiscard]] QRect targetClientRect() const;

    /** Cập nhật overlay từ box OCR đã tích luỹ theo vùng. */
    void updateOverlay(const QSize& frameSize);

    MainWindow*               m_mainWindow{nullptr};
    RegionEditorWindow*       m_regionEditor{nullptr};
    RegionSnipperOverlay*     m_snipperOverlay{nullptr};
    RegionHighlightOverlay*   m_highlightOverlay{nullptr};
    SettingsDialog*           m_settingsDialog{nullptr};
    MiniFloatBar*             m_miniFloatBar{nullptr};
    TrayIconManager*          m_trayManager{nullptr};
    EZTranslator::WgcWindowCapture* m_wgcCapture{nullptr};
    EZTranslator::GdiWindowCapture* m_gdiCapture{nullptr};
    EZTranslator::IWindowCapture*   m_capture{nullptr}; ///< backend đang hoạt động
    EZTranslator::RoiPreviewDialog* m_roiPreviewDialog{nullptr};
    EZTranslator::TextOverlay*      m_textOverlay{nullptr};
    EZTranslator::RegionManager      m_regionManager;
    EZTranslator::ChangeDetector     m_changeDetector;
    EZTranslator::OcrEngine          m_ocrEngine;
    bool                             m_ocrLoadAttempted{false};
    /// Box đã OCR theo từng vùng (toạ độ frame), giữ lại để overlay luôn đầy đủ
    /// kể cả khi vùng đó không đổi ở frame hiện tại.
    QHash<QString, QList<EZTranslator::OcrTextBox>> m_regionTextBoxes;

    EZTranslator::TranslationState m_state{EZTranslator::TranslationState::Idle};
    EZTranslator::AppSettings m_settings;
    QList<EZTranslator::TranslationRegion> m_regions;
    QRect m_currentScreenRect;
    bool  m_captureStartedForPreview{false};
};
