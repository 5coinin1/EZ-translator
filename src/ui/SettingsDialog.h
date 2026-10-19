#pragma once
#include <QDialog>
#include "core/Types.h"

class QListWidget;
class QStackedWidget;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QKeySequenceEdit;
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

private:
    void buildUi();
    QWidget* buildGeneralPage();
    QWidget* buildTranslationPage();
    QWidget* buildOverlayPage();
    QWidget* buildOcrPage();
    QWidget* buildHotkeysPage();

    void updatePreview();

    /** Chạy THẬT engine đang chọn trên câu mẫu (luồng nền) và báo kết quả trong dialog. */
    void probeTranslationEngine();

    QListWidget*    m_sidebar{nullptr};
    QStackedWidget* m_stack{nullptr};
    EZTranslator::AppSettings m_settings;

    // Translation page widgets
    QComboBox*       m_translationEngine{nullptr};
    QLineEdit*       m_googleApiKey{nullptr};
    QLineEdit*       m_aiEndpoint{nullptr};
    QLineEdit*       m_aiApiKey{nullptr};
    QLineEdit*       m_aiModel{nullptr};
    QPlainTextEdit*  m_aiPrompt{nullptr};
    QWidget*         m_aiGroup{nullptr};
    QPushButton*     m_probeBtn{nullptr};
    QLabel*          m_probeResult{nullptr};

    // General page widgets
    ToggleSwitch*    m_startWithWindows{nullptr};
    ToggleSwitch*    m_minimizeToTray{nullptr};

    // OCR page widgets
    QComboBox*       m_ocrEngine{nullptr};
    ToggleSwitch*    m_ocrGpu{nullptr};
    ToggleSwitch*    m_ocrBinarize{nullptr};

    // Hotkeys page widgets
    QKeySequenceEdit* m_hotkeyToggle{nullptr};
    QKeySequenceEdit* m_hotkeyRegion{nullptr};
    QKeySequenceEdit* m_hotkeyShowRegion{nullptr};
    QKeySequenceEdit* m_hotkeySettings{nullptr};

    // Appearance preview widgets
    class QFrame* m_previewFrame{nullptr};
    class QLabel* m_previewTextLabel{nullptr};
};
