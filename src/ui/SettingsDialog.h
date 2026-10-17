#pragma once

#include <QDialog>
#include "core/Types.h"

class QListWidget;
class QStackedWidget;
class QFrame;
class QLabel;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    [[nodiscard]] EZTranslator::AppSettings settings() const
    {
        return m_settings;
    }

    void setSettings(const EZTranslator::AppSettings &settings);

    static EZTranslator::AppSettings loadSettings();

    static bool saveSettings(
        const EZTranslator::AppSettings &settings);

signals:
    void settingsSaved(const EZTranslator::AppSettings &settings);

private:
    void buildUi();

    QWidget *buildGeneralPage();
    QWidget *buildOverlayPage();
    QWidget *buildOcrPage();
    QWidget *buildHotkeysPage();

    void updatePreview();

    QListWidget *m_sidebar{nullptr};
    QStackedWidget *m_stack{nullptr};

    EZTranslator::AppSettings m_settings;

    QFrame *m_previewFrame{nullptr};
    QLabel *m_previewTextLabel{nullptr};
};