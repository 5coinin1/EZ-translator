#pragma once

#include <QString>
#include <QColor>
#include <QRectF>
#include <QList>
#include <QIcon>
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

    bool enabled{true};              ///< false → RegionManager bỏ qua region này khi extract

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
    QIcon icon;
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
    QString sourceLanguage{"en"};         // ngôn ngữ nguồn cho OCR ("en", "zh", ...)
    bool ocrUseGpu{true};                 // true = thử GPU (DirectML), tự fallback CPU
    int cacheCapacityLines{5000};

    // Nhóm Hiển thị & Hiệu suất
    int captureFps{30};
    bool skipStaticFrames{true};
    bool useGpuAcceleration{false};
    double overlayOpacity{1.0};

    // Nhóm Giao diện (Appearance - phong cách Translumo)
    QColor windowColor{QColor(15, 23, 42, 230)};  // Window color
    QColor fontColor{QColor(255, 255, 255)};       // Font color
    int fontSize{15};                              // Font size (pt)
    bool isBold{true};                             // In đậm
    int lineSpacing{14};                           // Khoảng cách dòng
    bool keepSourceFormatting{false};              // Giữ định dạng gốc
    int textAlignment{0};                          // 0: Left, 1: Center, 2: Right
    bool autoClearWindow{false};                   // Tự động xóa cửa sổ
    bool excludeFromCapture{true};                 // Ẩn khỏi chụp/quay màn hình
    int windowOpacity{85};                         // Độ mờ đục (0 - 100)

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
