#pragma once

#include <QDialog>
#include <QList>
#include "region/RegionFrame.h"

class QLabel;
class QComboBox;
class QScrollArea;

namespace EZTranslator {

/**
 * @brief Cửa sổ preview trực tiếp kết quả cắt ROI từ RegionManager.
 *
 * Dùng để quan sát và xác nhận:
 *  - Cửa sổ đích đang được capture đúng
 *  - Toạ độ ROI cắt đúng vị trí chữ/nội dung
 *  - Kích thước pixel và chất lượng ảnh cắt ra
 */
class RoiPreviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RoiPreviewDialog(QWidget* parent = nullptr);
    ~RoiPreviewDialog() override = default;

    /** Cập nhật danh sách các RegionFrame nhận được từ pipeline */
    void updateFrames(const QList<RegionFrame>& frames);

signals:
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onRegionSelected(int index);
    void onZoomModeChanged(int index);

private:
    void setupUi();
    void renderCurrentFrame();

    QComboBox*   m_regionCombo{nullptr};
    QComboBox*   m_zoomCombo{nullptr};
    QLabel*      m_statusLabel{nullptr};
    QLabel*      m_imageLabel{nullptr};
    QScrollArea* m_scrollArea{nullptr};

    QLabel*      m_sizeLabel{nullptr};
    QLabel*      m_normRectLabel{nullptr};
    QLabel*      m_sourceSizeLabel{nullptr};
    QLabel*      m_timestampLabel{nullptr};

    QList<RegionFrame> m_latestFrames;
    int                m_currentRegionIndex{0};
    int                m_frameCount{0};
};

} // namespace EZTranslator
