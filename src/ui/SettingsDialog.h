#pragma once
#include <QDialog>
#include <QHash>
#include <QStringList>
#include "core/Types.h"

class QListWidget;
class QStackedWidget;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QKeySequenceEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QShowEvent;
class QTimer;
class QUrl;
class ToggleSwitch;

/**
 * SettingsDialog – Màn hình cài đặt Master-Detail.
 *
 * Sidebar trái: 7 mục danh mục.
 * Phần phải: QStackedWidget chứa form tương ứng từng mục.
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    /** Trả về cài đặt hiện tại sau khi người dùng bấm Lưu */
    [[nodiscard]] EZTranslator::AppSettings settings() const { return m_settings; }

    /** Nạp cài đặt hiện có vào các control (gọi trước khi mở dialog). */
    void setSettings(const EZTranslator::AppSettings& settings);

signals:
    void settingsSaved(const EZTranslator::AppSettings& settings);

protected:
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void showEvent(QShowEvent* event) override;

private:
    void buildUi();
    QWidget* buildGeneralPage();
    QWidget* buildTranslationPage();
    QWidget* buildOverlayPage();
    QWidget* buildOcrPage();
    QWidget* buildHotkeysPage();

    void updatePreview();
    void updateOcrInfo();
    void updateTranslationEngineVisibility();

    void scheduleModelFetch();
    void fetchModels(bool force = false);
    void startModelRequest(const QUrl& url);
    void applyModelList(const QStringList& models);
    void setModelStatus(const QString& text);

    /** Chạy THẬT engine đang chọn trên câu mẫu (luồng nền) và báo kết quả trong dialog. */
    void probeTranslationEngine();

    QListWidget*    m_sidebar{nullptr};
    QStackedWidget* m_stack{nullptr};
    EZTranslator::AppSettings m_settings;

    // Translation page widgets
    QComboBox*       m_translationEngine{nullptr};
    QWidget*         m_deviceGroup{nullptr};
    QWidget*         m_googleGroup{nullptr};
    QComboBox*       m_translateDevice{nullptr};
    QLineEdit*       m_googleApiKey{nullptr};
    QLineEdit*       m_aiEndpoint{nullptr};
    QLineEdit*       m_aiApiKey{nullptr};
    QComboBox*       m_aiModel{nullptr};
    QPushButton*     m_aiModelRefresh{nullptr};
    QLabel*          m_aiModelStatus{nullptr};
    QPlainTextEdit*  m_aiPrompt{nullptr};
    QWidget*         m_aiGroup{nullptr};

    QNetworkAccessManager* m_modelManager{nullptr};
    QNetworkReply*         m_modelReply{nullptr};
    QTimer*                m_modelDebounce{nullptr};
    QString                m_modelCacheKey;
    QStringList            m_modelCache;
    QString                m_modelRequestKey;
    QString                m_modelFallbackUrl;
    QPushButton*     m_probeBtn{nullptr};
    QLabel*          m_probeResult{nullptr};

    // General page widgets
    ToggleSwitch*    m_startWithWindows{nullptr};
    ToggleSwitch*    m_minimizeToTray{nullptr};

    // OCR page widgets
    QLabel*          m_ocrModelLabel{nullptr};
    QComboBox*       m_ocrDetDevice{nullptr};
    QComboBox*       m_ocrRecDevice{nullptr};
    QPushButton*     m_cudaInstallBtn{nullptr};
    QLabel*          m_cudaPackStatus{nullptr};
    ToggleSwitch*    m_ocrBinarize{nullptr};
    bool             m_loadingSettings{false};

    // Hotkeys page widgets
    QHash<QString, QKeySequenceEdit*> m_hotkeyEdits;

    // Appearance preview widgets
    ToggleSwitch*    m_novelModeSwitch{nullptr};
    ToggleSwitch*    m_gameDialogModeSwitch{nullptr};
    QComboBox*       m_fontFamilyCombo{nullptr};
    QComboBox*       m_textWindowStyleCombo{nullptr};
    ToggleSwitch*    m_showSourceSwitch{nullptr};
    class QFrame* m_previewFrame{nullptr};
    class QLabel* m_previewTextLabel{nullptr};
};
