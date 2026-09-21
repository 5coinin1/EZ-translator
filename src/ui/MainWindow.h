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

    /** Lấy handle HWND của cửa sổ đang được chọn */
    quintptr selectedWindowHandle() const;

    /** Làm mới danh sách các cửa sổ đang chạy */
    void refreshWindowList();

signals:
    void requestStartTranslation();
    void requestOpenRegionEditor();
    void requestOpenSettings();

    void targetWindowSelected(quintptr handle, const QString& title, const QString& processName);
    void profileSelected(const QString& profileId);

public slots:
    /** Gọi từ AppController khi state thay đổi */
    void onTranslationStarted();
    void onTranslationStopped();

private slots:
    void onWindowIndexChanged(int index);

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
