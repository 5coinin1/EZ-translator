#pragma once

#include <QString>
#include <QColor>
#include <QRectF>
#include <QList>
#include <utility>

namespace EZTranslator {

/**
 * @brief Tọa độ chuẩn hóa trong khoảng 0.0 -> 1.0
 * Giúp vùng dịch (ROI) độc lập với kích thước và độ phân giải của cửa sổ đích.
 */
struct NormalizedRect {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return width > 0.0 && height > 0.0 &&
               x >= 0.0 && y >= 0.0 &&
               (x + width) <= 1.05 && (y + height) <= 1.05;
    }

    [[nodiscard]] QRectF toQRectF(double targetWidth, double targetHeight) const noexcept {
        return QRectF(x * targetWidth, y * targetHeight, width * targetWidth, height * targetHeight);
    }

    [[nodiscard]] static NormalizedRect fromQRectF(const QRectF& rect, double targetWidth, double targetHeight) noexcept {
        if (targetWidth <= 0.0 || targetHeight <= 0.0) return {};
        return {
            rect.x() / targetWidth,
            rect.y() / targetHeight,
            rect.width() / targetWidth,
            rect.height() / targetHeight
        };
    }
};

/**
 * @brief Định nghĩa một vùng cần dịch (ROI)
 */
struct TranslationRegion {
    QString id;
    int orderNumber{1};
    QString name;

    NormalizedRect normalizedRect;

    QColor tagColor{QColor(37, 99, 235)}; // Màu badge phân loại
    QString fontFamily{"Segoe UI"};
    int fontSize{14};
    QColor textColor{QColor(248, 250, 252)};
    QColor backgroundColor{QColor(17, 22, 34)}; // Màu che chữ gốc
};

/**
 * @brief Hồ sơ cấu hình các vùng dịch tương ứng một ứng dụng/màn hình
 */
struct Profile {
    QString id;
    QString name;
    QString targetExecutable;
    QString windowTitlePattern;

    QString sourceLanguage{"auto"};
    QString targetLanguage{"vi"};

    QList<TranslationRegion> regions;
};

/**
 * @brief Tóm tắt thông tin profile phục vụ hiển thị combobox/danh sách
 */
struct ProfileHeader {
    QString id;
    QString name;
    QString targetExecutable;
    int regionCount{0};
};

/**
 * @brief Thông tin cơ bản về một cửa sổ đang mở trên hệ điều hành
 */
struct WindowInfo {
    quintptr handle{0};          // Abstraction cho HWND để không include windows.h
    QString title;
    QString processName;
    bool isVisible{true};
};

/**
 * @brief Cấu hình cài đặt toàn hệ thống của ứng dụng
 */
struct AppSettings {
    // Nhóm Chung
    bool startWithWindows{false};
    bool minimizeToTrayOnClose{true};
    bool checkForUpdates{true};
    QString interfaceLanguage{"vi"};

    // Nhóm Dịch thuật & OCR (Options cho UI phase)
    QString translationEngine{"offline"}; // "offline", "google"
    QString ocrEngine{"windows_ocr"};     // "windows_ocr", "tesseract"
    int cacheCapacityLines{5000};

    // Nhóm Hiển thị & Hiệu suất
    int captureFps{30};
    bool skipStaticFrames{true};
    bool useGpuAcceleration{false};
    double overlayOpacity{1.0};

    // Nhóm Phím tắt
    QString toggleTranslationHotkey{"F8"};
};

/**
 * @brief Máy trạng thái dịch thuật của hệ thống
 */
enum class TranslationState {
    Idle,       ///< Đang ở màn hình chính, chưa dịch
    Running,    ///< Đang trong phiên dịch thời gian thực (Overlay/MiniFloat hoạt động)
    Paused      ///< Đang tạm ngưng dịch
};

} // namespace EZTranslator
