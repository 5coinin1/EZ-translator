#pragma once

#include "capture/IWindowCapture.h"
#include "core/Types.h"
#include "ocr/OcrWorker.h"
#include "region/RegionManager.h"

#include <QHash>
#include <QObject>

class MainWindow;
class OfflineTranslator;
class QDialog;
class QPlainTextEdit;
class RegionEditorWindow;
class RegionSnipperOverlay;
class RegionHighlightOverlay;
class SettingsDialog;
class MiniFloatBar;
class TrayIconManager;

namespace EZTranslator
{
    class GdiWindowCapture;
    class WgcWindowCapture;
    class RoiPreviewDialog;
    class TextOverlay;
} // namespace EZTranslator

class AppController : public QObject
{
    Q_OBJECT

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    void start();

public slots:
    void onStartTranslation();
    void onStopTranslation();
    void onToggleTranslation();

    void onOpenRegionEditor();

    void
    onRegionEditorSaved(const QList<EZTranslator::TranslationRegion> &regions);

    void onRegionEditorCancelled();

    void onRegionSnapped(const EZTranslator::NormalizedRect &rect,
                         const QRect &screenRect);

    void onRegionSnapCancelled();
    void onShowRegion();
    void onPreviewRoiRequested();
    void onPreviewClosed();

    void onOpenSettings();
    void onSettingsSaved(const EZTranslator::AppSettings &settings);

    void onShowMainWindow();
    void onQuit();

    void onTargetWindowSelected(quintptr handle, const QString &title,
                                const QString &processName);

    void onCaptureFrame(const EZTranslator::CapturedFrame &frame);

    void onCaptureError(EZTranslator::CaptureError error, const QString &detail);

private:
    void initConnections();
    void initTranslationUi();
    void resetTranslationSession();

    void onOcrCompleted(const EZTranslator::OcrResult &result);

    void applyTranslationResult(const QString &target);

    bool startCapture(quintptr windowHandle);
    [[nodiscard]] QRect targetClientRect() const;
    void updateOverlay(const QSize &frameSize);

    EZTranslator::OcrWorker *m_ocrWorker{nullptr};
    quint64 m_ocrRevision{0};

    QHash<QString, QImage> m_ocrImages;
    QHash<QString, QRect> m_observedRects;
    QSize m_observedFrameSize;

    QList<EZTranslator::OcrTextBox> m_overlayLayout;
    QList<EZTranslator::OcrTextBox> m_overlayOutput;

    QSize m_overlayFrameSize;
    QString m_lastTranslatedText;
    bool m_overlayFresh{false};

    OfflineTranslator *m_translator{nullptr};
    QDialog *m_translationWindow{nullptr};
    QPlainTextEdit *m_sourceText{nullptr};
    QPlainTextEdit *m_translatedText{nullptr};

    QString m_lastSubmittedText;
    qint64 m_acceptFramesAfter{0};

    MainWindow *m_mainWindow{nullptr};
    RegionEditorWindow *m_regionEditor{nullptr};
    RegionSnipperOverlay *m_snipperOverlay{nullptr};
    RegionHighlightOverlay *m_highlightOverlay{nullptr};
    SettingsDialog *m_settingsDialog{nullptr};
    MiniFloatBar *m_miniFloatBar{nullptr};
    TrayIconManager *m_trayManager{nullptr};

    EZTranslator::WgcWindowCapture *m_wgcCapture{nullptr};
    EZTranslator::GdiWindowCapture *m_gdiCapture{nullptr};
    EZTranslator::IWindowCapture *m_capture{nullptr};

    EZTranslator::RoiPreviewDialog *m_roiPreviewDialog{nullptr};
    EZTranslator::TextOverlay *m_textOverlay{nullptr};
    EZTranslator::RegionManager m_regionManager;

    EZTranslator::TranslationState m_state{EZTranslator::TranslationState::Idle};

    EZTranslator::AppSettings m_settings;
    QList<EZTranslator::TranslationRegion> m_regions;
    QRect m_currentScreenRect;
    bool m_captureStartedForPreview{false};
};