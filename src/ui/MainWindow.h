#pragma once

#include <QWidget>
#include "core/Types.h"

class TitleBar;
class StatusIndicator;
class AppComboBox;
class DisplayModeCard;
class QPushButton;
class QFrame;
class QLabel;

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

    /** Nhắc nhở người dùng cần chọn cửa sổ */
    void showSelectWindowWarning();

    /** Thông báo đã lưu vùng dịch và cập nhật thông tin vùng hiển thị */
    void setRegion(const EZTranslator::NormalizedRect& rect, const QRect& screenRect = QRect());
    void clearRegion();
    void showRegionSelectedSuccess();
    void setStatus(const QString& text, const QColor& color = {});
    void setShowRegionActive(bool active);

    [[nodiscard]] bool hasRegion() const { return m_hasRegion; }
    [[nodiscard]] EZTranslator::NormalizedRect currentRegion() const { return m_currentRegion; }
    [[nodiscard]] QRect currentScreenRect() const { return m_currentScreenRect; }

    enum class DisplayMode {
        FloatingWindow,
        ScreenOverlay
    };
    [[nodiscard]] DisplayMode displayMode() const;

signals:
    void requestStartTranslation();
    void requestOpenRegionEditor();
    void requestClearRegion();
    void requestShowRegion();
    void requestOpenSettings();
    void displayModeChanged(DisplayMode mode);

    void targetWindowSelected(quintptr handle, const QString& title, const QString& processName);

public slots:
    /** Gọi từ AppController khi state thay đổi */
    void onTranslationStarted();
    void onTranslationStopped();

private slots:
    void onWindowIndexChanged(int index);

private:
    void buildUi();
    void populateMockData();
    void updateRegionDisplay();

    TitleBar*        m_titleBar{nullptr};
    AppComboBox*     m_windowCombo{nullptr};
    AppComboBox*     m_srcLangCombo{nullptr};
    AppComboBox*     m_dstLangCombo{nullptr};

    // Section 3: Hiển thị bản dịch
    class DisplayModeCard* m_cardFloat{nullptr};
    class DisplayModeCard* m_cardOverlay{nullptr};

    // Section 4: Chọn vùng dịch & Hiển thị vùng
    QPushButton*     m_selectRegionBtn{nullptr};
    QPushButton*     m_clearRegionBtn{nullptr};
    QPushButton*     m_showRegionBtn{nullptr};
    QFrame*          m_regionDisplayFrame{nullptr};
    class RegionThumbnailWidget* m_regionThumbnail{nullptr};
    QLabel*          m_regionTitleLabel{nullptr};
    QLabel*          m_regionDetailLabel{nullptr};
    QLabel*          m_regionBadgeLabel{nullptr};

    bool                          m_hasRegion{false};
    EZTranslator::NormalizedRect  m_currentRegion;
    QRect                         m_currentScreenRect;

    StatusIndicator* m_statusIndicator{nullptr};
};
