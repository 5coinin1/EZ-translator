#pragma once

#include <QWidget>
#include "core/Types.h"

class TitleBar;
class StatusIndicator;
class AppComboBox;

/**
 * MainWindow – Màn hình chính của EZ-Translator.
 *
 * Layout:
 *   TitleBar
 *   ContentArea
 *     Section 1: Chọn cửa sổ cần dịch
 *     Section 2: Ngôn ngữ
 *     Section 3: Hồ sơ (Profile)
 *     Action buttons
 *   Footer bar (status + settings btn)
 */
class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

signals:
    void requestStartTranslation();
    void requestOpenRegionEditor();
    void requestOpenSettings();

    // Dùng mock data trong UI phase
    void targetWindowChanged(int index);
    void profileSelected(const QString& profileId);

public slots:
    /** Gọi từ AppController khi state thay đổi */
    void onTranslationStarted();
    void onTranslationStopped();

private:
    void buildUi();
    void populateMockData();

    TitleBar*        m_titleBar{nullptr};
    AppComboBox*     m_windowCombo{nullptr};
    AppComboBox*     m_srcLangCombo{nullptr};
    AppComboBox*     m_dstLangCombo{nullptr};
    AppComboBox*     m_profileCombo{nullptr};
    StatusIndicator* m_statusIndicator{nullptr};
};
