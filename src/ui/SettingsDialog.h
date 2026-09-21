#pragma once
#include <QDialog>
#include "core/Types.h"

class QListWidget;
class QStackedWidget;

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

signals:
    void settingsSaved(const EZTranslator::AppSettings& settings);

private:
    void buildUi();
    QWidget* buildGeneralPage();
    QWidget* buildTranslationPage();
    QWidget* buildOcrPage();
    QWidget* buildOverlayPage();
    QWidget* buildPerformancePage();
    QWidget* buildHotkeysPage();
    QWidget* buildAdvancedPage();

    QListWidget*    m_sidebar{nullptr};
    QStackedWidget* m_stack{nullptr};
    EZTranslator::AppSettings m_settings;
};
