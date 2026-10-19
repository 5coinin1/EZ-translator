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
class QCloseEvent;
class QMenu;
class QAction;

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

    /** Đặt cặp ngôn ngữ đang chọn trên giao diện (không phát tín hiệu). */
    void setLanguages(const QString& source, const QString& target);

    [[nodiscard]] QString sourceLanguage() const;
    [[nodiscard]] QString targetLanguage() const;

    /** Nhắc nhở người dùng cần chọn cửa sổ */
    void showSelectWindowWarning();

    /**
     * @brief Cập nhật chuỗi phím tắt hiển thị trên nút/nhãn (từ Cài đặt → Phím tắt).
     *
     * Phím tắt thật do AppController đăng ký toàn cục; đây chỉ là phần hiển thị.
     */
    void setHotkeyHints(const QString& startStopKey, const QString& selectRegionKey);

    /** Bật: bấm X chỉ ẩn xuống khay thay vì thoát app (Cài đặt → Chung). */
    void setCloseToTray(bool enabled);

    /** Thông báo đã lưu vùng dịch và cập nhật thông tin vùng hiển thị */
    void setRegion(const EZTranslator::NormalizedRect& rect, const QRect& screenRect = QRect());
    void clearRegion();

    /**
     * @brief Nạp danh sách vùng dịch vào dropdown "Chọn vùng dịch".
     *
     * Dropdown là nguồn chọn vùng đang thao tác: Thêm/Sửa/Xoá đều áp dụng lên vùng
     * đang được chọn trong danh sách này.
     */
    void setRegions(const QList<EZTranslator::TranslationRegion>& regions, int current);
    [[nodiscard]] int currentRegionIndex() const;
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
    void requestStopTranslation();
    void requestOpenRegionEditor();
    void requestClearRegion();
    void requestShowRegion();
    void requestPreviewRoi();
    /** Thêm một vùng dịch mới (khoanh bằng chuột trên màn hình). */
    void requestAddRegion();
    /** Sửa (khoanh lại) vùng dịch đang chọn trong dropdown. */
    void requestEditRegion(int index);
    /** Xoá vùng dịch đang chọn khỏi danh sách. */
    void requestRemoveRegion(int index);
    /** Người dùng chọn một vùng khác trong dropdown. */
    void regionSelected(int index);
    /** Mở cửa sổ danh sách vùng (thêm/xoá/sửa nhiều vùng bằng layout editor). */
    void requestOpenRegionList();
    void requestOpenSettings();
    void displayModeChanged(DisplayMode mode);

    /** Người dùng đổi ngôn ngữ gốc/đích trên giao diện chính. */
    void languagesChanged(const QString& source, const QString& target);

    void targetWindowSelected(quintptr handle, const QString& title, const QString& processName);

public slots:
    /** Gọi từ AppController khi state thay đổi */
    void onTranslationStarted();
    void onTranslationStopped();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onWindowIndexChanged(int index);
    void onSelectRegionClicked();
    void onStartButtonClicked();
    void onRegionComboChanged(int index);
    void onAddRegionClicked();
    void onEditRegionClicked();
    void onRemoveRegionClicked();
    void onManageRegionsClicked();

private:
    void buildUi();    void populateInitialData();
    void updateRegionDisplay();
    /** Ẩn/hiện Section "Chọn vùng dịch" theo chế độ hiển thị (chỉ Overlay mới cần vùng). */
    void applyDisplayModeUi(DisplayMode mode);

    TitleBar*        m_titleBar{nullptr};
    AppComboBox*     m_windowCombo{nullptr};
    AppComboBox*     m_srcLangCombo{nullptr};
    AppComboBox*     m_dstLangCombo{nullptr};

    // Section 3: Hiển thị bản dịch (segmented control 2 lựa chọn)
    QPushButton* m_cardFloat{nullptr};
    QPushButton* m_cardOverlay{nullptr};

    // Section 4: Chọn vùng dịch — gọn: [dropdown] [＋ Thêm] [⋯ menu thao tác]
    QWidget*         m_regionSection{nullptr}; ///< cả Section 4 (ẩn ở chế độ Cửa sổ nổi)
    AppComboBox*     m_regionCombo{nullptr};   ///< dropdown danh sách vùng dịch
    QPushButton*     m_selectRegionBtn{nullptr}; ///< "＋ Thêm vùng"
    QPushButton*     m_regionMenuBtn{nullptr};   ///< "⋯" mở menu thao tác trên vùng đang chọn
    QMenu*           m_regionMenu{nullptr};
    QAction*         m_editRegionAction{nullptr};
    QAction*         m_removeRegionAction{nullptr};
    QAction*         m_showRegionAction{nullptr};
    QAction*         m_previewRoiAction{nullptr};
    QAction*         m_manageRegionsAction{nullptr};
    QLabel*          m_regionHintLabel{nullptr}; ///< một dòng thông tin vùng đang chọn
    bool             m_updatingRegions{false}; ///< đang nạp dropdown → bỏ qua tín hiệu đổi index

    QPushButton*     m_startBtn{nullptr};
    bool             m_isTranslating{false};

    QString          m_startStopHotkey{"F8"}; ///< hiển thị trên nút Bắt đầu/Dừng
    QString          m_selectRegionHotkey{"F6"}; ///< hiển thị trong nhãn vùng dịch
    bool             m_closeToTray{false};    ///< X = ẩn xuống khay (Cài đặt → Chung)

    bool                          m_updatingLanguages{false};
    bool                          m_hasRegion{false};
    EZTranslator::NormalizedRect  m_currentRegion;
    QRect                         m_currentScreenRect;

    StatusIndicator* m_statusIndicator{nullptr};
};
