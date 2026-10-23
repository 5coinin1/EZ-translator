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
    QString translationEngine{"auto"}; // "auto", "offline", "google", "ai"
    QString googleApiKey;              // rỗng → dùng endpoint miễn phí; có key → Cloud Translation API
    QString aiEndpoint{"https://api.openai.com/v1/chat/completions"};
    QString aiApiKey;                  // để trống nếu endpoint local không cần key
    QString aiModel{"gpt-4o-mini"};
    QString aiSystemPrompt{
        QStringLiteral("Bạn là một dịch giả chuyên nghiệp, am hiểu game, truyện và hội thoại. "
                       "Dịch sát nghĩa nhưng tự nhiên, giữ đúng sắc thái, cảm xúc, cách xưng hô "
                       "và tên riêng. Không thêm bình luận hay giải thích.")};
    QString ocrEngine{"paddleocr"};       // engine OCR đang dùng thật ("paddleocr"; các engine khác chưa hỗ trợ)
    QString sourceLanguage{"en"};         // ngôn ngữ nguồn cho OCR/translate ("auto", "en", "zh", ...)
    QString targetLanguage{"vi"};         // ngôn ngữ đích của bản dịch
    bool ocrUseGpu{true};                 // true = thử GPU (DirectML), tự fallback CPU
    bool ocrBinarize{false};              // thử nghiệm: nhị phân hoá ảnh vùng trước khi OCR
    int cacheCapacityLines{5000};         // số dòng tối đa giữ trong cache bản dịch

    // Nhóm Hiển thị & Hiệu suất
    int captureFps{30};
    bool skipStaticFrames{true};
    bool useGpuAcceleration{false};
    double overlayOpacity{1.0};

    // Nhóm Giao diện (Appearance - phong cách Translumo)
    QColor windowColor{QColor(15, 23, 42, 230)};  // Window color
    QColor fontColor{QColor(255, 255, 255)};       // Font color
    QString fontFamily{"Segoe UI"};                 // Font for hover/floating text
    int textWindowStyle{1};                        // 0 Solid, 1 Glass, 2 Outline, 3 Paper
    bool textWindowShowSource{true};               // Show source text in hover/floating
    int fontSize{15};                              // Font size (pt)
    bool isBold{true};                             // In đậm
    int lineSpacing{14};                           // Khoảng cách dòng (pt, 0 = theo cỡ chữ)
    bool keepSourceFormatting{true};     // true = cỡ chữ bám theo dòng gốc (auto-fit)
    bool novelMode{false};               // true = luôn dùng layout đọc truyện (paper)
    int textAlignment{0};                          // 0: Left, 1: Center, 2: Right
    bool autoClearWindow{false};                   // Tự động xóa cửa sổ (khi dừng dịch)
    bool excludeFromCapture{true};                 // Ẩn khỏi chụp/quay màn hình
    int windowOpacity{100};                         // Độ mờ nội dung overlay (0 - 100)

    // Nhóm Phím tắt (đăng ký toàn cục qua Win32 RegisterHotKey)
    QString toggleTranslationHotkey{"F8"};
    QString selectRegionHotkey{"F6"};
    QString showRegionHotkey{"F7"};
    QString openSettingsHotkey{"Ctrl+,"};
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
