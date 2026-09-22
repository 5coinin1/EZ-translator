#include "RoiPreviewDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QScrollArea>
#include <QPushButton>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QDateTime>
#include <QPixmap>
#include <algorithm>

namespace EZTranslator {

RoiPreviewDialog::RoiPreviewDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Preview ROI (Vùng dịch thực tế) - EZ Translator");
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    resize(920, 640);
    setMinimumSize(680, 480);

    setupUi();
}

void RoiPreviewDialog::setupUi()
{
    setStyleSheet(R"(
        QDialog {
            background-color: #0b111e;
            color: #f8fafc;
            font-family: 'Segoe UI', sans-serif;
        }
        QLabel {
            color: #cbd5e1;
            font-size: 9pt;
        }
        QComboBox {
            background-color: #162236;
            color: #f8fafc;
            border: 1px solid #28374d;
            border-radius: 6px;
            padding: 5px 12px;
            font-size: 9pt;
            min-height: 28px;
        }
        QComboBox::drop-down {
            border: none;
            width: 22px;
        }
        QComboBox QAbstractItemView {
            background-color: #162236;
            color: #f8fafc;
            selection-background-color: #2563eb;
            border: 1px solid #28374d;
        }
        QScrollArea {
            background-color: #070b12;
            border: 1px solid #1e293b;
            border-radius: 10px;
        }
        QPushButton {
            background-color: #162236;
            color: #94a3b8;
            border: 1px solid #28374d;
            border-radius: 6px;
            padding: 5px 14px;
            font-size: 9pt;
        }
        QPushButton:hover {
            background-color: #24354f;
            color: #ffffff;
            border-color: #3b82f6;
        }
    )");

    auto* rootLay = new QVBoxLayout(this);
    rootLay->setContentsMargins(16, 16, 16, 16);
    rootLay->setSpacing(12);

    // ── Header / Top controls ────────────────────────────────────────────────
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(12);

    auto* comboLabel = new QLabel("Vùng dịch:", this);
    comboLabel->setStyleSheet("font-weight: 600; color: #38bdf8; font-size: 9.5pt;");
    topRow->addWidget(comboLabel);

    m_regionCombo = new QComboBox(this);
    m_regionCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_regionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RoiPreviewDialog::onRegionSelected);
    topRow->addWidget(m_regionCombo, 3);

    auto* zoomLabel = new QLabel("Chế độ xem:", this);
    zoomLabel->setStyleSheet("font-weight: 600; color: #94a3b8; font-size: 9pt;");
    topRow->addWidget(zoomLabel);

    m_zoomCombo = new QComboBox(this);
    m_zoomCombo->addItem("Tự động vừa khung (Fit)", 0);
    m_zoomCombo->addItem("Kích thước gốc (100%)",   100);
    m_zoomCombo->addItem("Phóng to 150%",           150);
    m_zoomCombo->addItem("Phóng to 200%",           200);
    m_zoomCombo->addItem("Phóng to 300%",           300);
    connect(m_zoomCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RoiPreviewDialog::onZoomModeChanged);
    topRow->addWidget(m_zoomCombo, 2);

    m_statusLabel = new QLabel("🟡 Đang chờ frame...", this);
    m_statusLabel->setStyleSheet("color: #94a3b8; font-size: 9pt;");
    topRow->addWidget(m_statusLabel);

    rootLay->addLayout(topRow);

    // ── Image Preview Area ───────────────────────────────────────────────────
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setAlignment(Qt::AlignCenter);

    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setText("Chưa có ảnh ROI. Hãy đảm bảo cửa sổ đích không bị thu nhỏ (minimize).");
    m_imageLabel->setStyleSheet("color: #64748b; font-style: italic; padding: 30px; font-size: 10pt;");

    m_scrollArea->setWidget(m_imageLabel);
    rootLay->addWidget(m_scrollArea, 1);

    // ── Info Bar ─────────────────────────────────────────────────────────────
    auto* infoFrame = new QFrame(this);
    infoFrame->setStyleSheet(R"(
        QFrame {
            background-color: #121a28;
            border: 1px solid #1e293b;
            border-radius: 8px;
            padding: 4px;
        }
        QLabel {
            font-size: 9pt;
            color: #94a3b8;
        }
    )");
    auto* infoLay = new QGridLayout(infoFrame);
    infoLay->setContentsMargins(12, 8, 12, 8);
    infoLay->setHorizontalSpacing(20);
    infoLay->setVerticalSpacing(6);

    m_sizeLabel       = new QLabel("Kích thước ROI: --", infoFrame);
    m_normRectLabel   = new QLabel("Tọa độ tỷ lệ: --", infoFrame);
    m_sourceSizeLabel = new QLabel("Cửa sổ nguồn: --", infoFrame);
    m_timestampLabel  = new QLabel("Frame count: 0", infoFrame);

    infoLay->addWidget(m_sizeLabel,       0, 0);
    infoLay->addWidget(m_normRectLabel,   0, 1);
    infoLay->addWidget(m_sourceSizeLabel, 1, 0);
    infoLay->addWidget(m_timestampLabel,  1, 1);

    rootLay->addWidget(infoFrame);
}

void RoiPreviewDialog::updateFrames(const QList<RegionFrame>& frames)
{
    if (frames.isEmpty()) {
        return;
    }

    m_latestFrames = frames;
    m_frameCount++;

    // Cập nhật danh sách region vào combobox nếu số lượng hoặc id thay đổi
    bool needRefreshCombo = (m_regionCombo->count() != frames.size());
    if (!needRefreshCombo) {
        for (int i = 0; i < frames.size(); ++i) {
            if (m_regionCombo->itemData(i).toString() != frames[i].regionId) {
                needRefreshCombo = true;
                break;
            }
        }
    }

    if (needRefreshCombo) {
        QSignalBlocker blocker(m_regionCombo);
        m_regionCombo->clear();
        for (int i = 0; i < frames.size(); ++i) {
            QString label = QString("%1 (%2)")
                                .arg(frames[i].regionName.isEmpty() ? QString("Vùng %1").arg(i + 1) : frames[i].regionName)
                                .arg(frames[i].regionId);
            m_regionCombo->addItem(label, frames[i].regionId);
        }
        if (m_currentRegionIndex >= frames.size()) {
            m_currentRegionIndex = 0;
        }
        m_regionCombo->setCurrentIndex(m_currentRegionIndex);
    }

    m_statusLabel->setText(QString("🟢 Live (Frame #%1 - 30 FPS)").arg(m_frameCount));
    m_statusLabel->setStyleSheet("color: #10b981; font-weight: 600; font-size: 9pt;");

    renderCurrentFrame();
}

void RoiPreviewDialog::onRegionSelected(int index)
{
    if (index >= 0 && index < m_latestFrames.size()) {
        m_currentRegionIndex = index;
        renderCurrentFrame();
    }
}

void RoiPreviewDialog::onZoomModeChanged(int /*index*/)
{
    renderCurrentFrame();
}

void RoiPreviewDialog::renderCurrentFrame()
{
    if (m_latestFrames.isEmpty() || m_currentRegionIndex >= m_latestFrames.size()) {
        return;
    }

    const RegionFrame& rf = m_latestFrames[m_currentRegionIndex];

    if (rf.image.isNull() || rf.image.width() <= 0 || rf.image.height() <= 0) {
        m_imageLabel->setText("Ảnh ROI rỗng hoặc không hợp lệ.");
        return;
    }

    QPixmap rawPix = QPixmap::fromImage(rf.image);
    QPixmap displayPix;
    QString zoomInfo;

    int zoomMode = m_zoomCombo ? m_zoomCombo->currentData().toInt() : 0;

    if (zoomMode == 0) {
        // Tự động vừa khung nhìn (Fit to window)
        QSize viewSize = m_scrollArea->viewport()->size() - QSize(20, 20);
        if (viewSize.width() > 50 && viewSize.height() > 50) {
            displayPix = rawPix.scaled(viewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            double scaleFactor = (double)displayPix.width() / rawPix.width() * 100.0;
            zoomInfo = QString("Fit: %1%").arg(qRound(scaleFactor));
        } else {
            displayPix = rawPix;
            zoomInfo = "100%";
        }
    } else {
        // Zoom cố định: 100%, 150%, 200%, 300%
        double scale = (double)zoomMode / 100.0;
        if (zoomMode == 100) {
            displayPix = rawPix;
            zoomInfo = "100%";
        } else {
            QSize scaledSize(qRound(rawPix.width() * scale), qRound(rawPix.height() * scale));
            displayPix = rawPix.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            zoomInfo = QString("%1%").arg(zoomMode);
        }
    }

    m_imageLabel->setPixmap(displayPix);
    m_imageLabel->resize(displayPix.size());

    // Cập nhật thông tin chi tiết
    m_sizeLabel->setText(QString("Kích thước ROI: <b style='color:#38bdf8'>%1 × %2 px</b> (%3)")
                             .arg(rf.image.width())
                             .arg(rf.image.height())
                             .arg(zoomInfo));

    double nx = (rf.sourceFrameSize.width() > 0) ? (double)rf.pixelRect.x() / rf.sourceFrameSize.width() : 0.0;
    double ny = (rf.sourceFrameSize.height() > 0) ? (double)rf.pixelRect.y() / rf.sourceFrameSize.height() : 0.0;
    double nw = (rf.sourceFrameSize.width() > 0) ? (double)rf.pixelRect.width() / rf.sourceFrameSize.width() : 0.0;
    double nh = (rf.sourceFrameSize.height() > 0) ? (double)rf.pixelRect.height() / rf.sourceFrameSize.height() : 0.0;

    m_normRectLabel->setText(QString("Tọa độ tỷ lệ: [%1, %2, %3, %4] (pixel: %5,%6)")
                                 .arg(nx, 0, 'f', 2)
                                 .arg(ny, 0, 'f', 2)
                                 .arg(nw, 0, 'f', 2)
                                 .arg(nh, 0, 'f', 2)
                                 .arg(rf.pixelRect.x())
                                 .arg(rf.pixelRect.y()));

    m_sourceSizeLabel->setText(QString("Cửa sổ nguồn: <b>%1 × %2 px</b>")
                                   .arg(rf.sourceFrameSize.width())
                                   .arg(rf.sourceFrameSize.height()));

    QString timeStr = QDateTime::fromMSecsSinceEpoch(rf.timestamp).toString("HH:mm:ss.zzz");
    m_timestampLabel->setText(QString("Frame #%1 (%2)").arg(m_frameCount).arg(timeStr));
}

void RoiPreviewDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (m_zoomCombo && m_zoomCombo->currentData().toInt() == 0) {
        renderCurrentFrame();
    }
}

void RoiPreviewDialog::closeEvent(QCloseEvent* event)
{
    emit closed();
    event->accept();
}

} // namespace EZTranslator
