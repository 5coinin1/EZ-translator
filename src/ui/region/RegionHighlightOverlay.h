#pragma once

#include <QWidget>
#include <QRect>
#include <QTimer>
#include <QPixmap>
#include "core/Types.h"

/**
 * @brief RegionHighlightOverlay – Hiển thị khung đỏ tại vùng dịch đã chọn.
 *
 * Dùng cùng approach với RegionSnipperOverlay: chụp screenshot, show fullscreen,
 * vẽ khung đỏ tại đúng tọa độ. Đảm bảo hoạt động mọi cấu hình Windows.
 *
 * Đóng bằng: click chuột, nhấn ESC, hoặc timeout tự động.
 */
class RegionHighlightOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit RegionHighlightOverlay(QWidget* parent = nullptr);

    /**
     * Hiển thị khung đỏ tại đúng tọa độ màn hình thực tế (pixel tuyệt đối).
     * Tự chụp screenshot làm nền để đảm bảo overlay luôn hiển thị được.
     */
    void showScreenRect(const QRect& screenRect, int autoHideMs = 5000);

    /**
     * Hiển thị khung đỏ lên cửa sổ đích (dùng tọa độ chuẩn hóa).
     */
    void showRegion(const QRect& targetWindowRect,
                    const EZTranslator::NormalizedRect& normalizedRegion,
                    int autoHideMs = 5000);

    void hideOverlay();

    bool isShowing() const { return isVisible(); }

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void doShow(const QRect& screenRect, int autoHideMs);

    QPixmap m_background;           ///< Screenshot làm nền
    QRect   m_highlightRect;        ///< Vùng cần tô đỏ (tọa độ màn hình thực tế)
    QTimer* m_autoHideTimer{nullptr};

    // Hiệu ứng pulse (nhấp nháy nhẹ)
    QTimer* m_pulseTimer{nullptr};
    int     m_pulseStep{0};
};
