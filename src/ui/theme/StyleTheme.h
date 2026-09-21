#pragma once

#include <QString>
#include <QColor>
#include <QFont>

/**
 * StyleTheme – Nguồn cơn duy nhất cho toàn bộ màu sắc, font, spacing và QSS.
 * Không hard-code giá trị màu ở bất kỳ Widget nào khác.
 */
namespace StyleTheme {

// ─── Colors ──────────────────────────────────────────────────────────────────
inline constexpr auto ColorBackground      = "#111622";
inline constexpr auto ColorSurface         = "#192030";
inline constexpr auto ColorSurfaceHigh     = "#1e2a3d";
inline constexpr auto ColorBorder          = "#283248";
inline constexpr auto ColorBorderFocus     = "#3b5280";

inline constexpr auto ColorPrimary         = "#2563eb";
inline constexpr auto ColorPrimaryHover    = "#1d4ed8";
inline constexpr auto ColorPrimaryPressed  = "#1e40af";

inline constexpr auto ColorSuccess         = "#22c55e";
inline constexpr auto ColorDanger          = "#ef4444";
inline constexpr auto ColorWarning         = "#f59e0b";

inline constexpr auto ColorTextPrimary     = "#f8fafc";
inline constexpr auto ColorTextSecondary   = "#94a3b8";
inline constexpr auto ColorPlaceholder     = "#64748b";

inline constexpr auto ColorSidebarActive   = "#1e3a5f";
inline constexpr auto ColorSidebarHover    = "#1a2d4a";

// ─── Radii & Spacing ─────────────────────────────────────────────────────────
inline constexpr int RadiusSmall    = 6;
inline constexpr int RadiusMedium   = 8;
inline constexpr int RadiusLarge    = 12;
inline constexpr int RadiusCard     = 10;

inline constexpr int SpacingXS = 4;
inline constexpr int SpacingS  = 8;
inline constexpr int SpacingM  = 12;
inline constexpr int SpacingL  = 18;
inline constexpr int SpacingXL = 24;

// ─── Font ────────────────────────────────────────────────────────────────────
inline constexpr auto FontFamily = "Segoe UI";

inline constexpr int FontSizeCaption  = 8;
inline constexpr int FontSizeBody     = 10;
inline constexpr int FontSizeLabel    = 10;
inline constexpr int FontSizeSubtitle = 11;
inline constexpr int FontSizeTitle    = 14;
inline constexpr int FontSizeH2       = 16;

// ─── Stylesheet generators ───────────────────────────────────────────────────

/** QSS toàn cục – áp dụng lên QApplication */
QString globalStylesheet();

/** QSS cho một vùng card / surface riêng */
QString cardStyle(int radius = RadiusCard);

} // namespace StyleTheme
