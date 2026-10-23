#pragma once

#include "core/Types.h"

namespace EZTranslator::SettingsStore {

/**
 * @brief Lưu/nạp TOÀN BỘ AppSettings vào QSettings.
 *
 * Trước đây chỉ nhóm "Dịch thuật" (engine/API key/ngôn ngữ) được lưu ở nhóm
 * `translation`; mọi lựa chọn ở "Giao diện", "OCR", "Phím tắt", "Chung" mất
 * sau khi tắt app (cảm giác "cài đặt là giả"). Store này lưu tất cả dưới nhóm
 * `settings/v1`, đồng thời migrate giá trị cũ từ nhóm `translation` ở lần chạy
 * đầu sau khi nâng cấp.
 *
 * Giá trị ghi ra đều là kiểu văn bản/số/bool đơn giản; màu lưu dạng
 * "#AARRGGBB" để giữ cả alpha (màu che chữ gốc).
 */
void load(AppSettings& settings);

/** Ghi đè toàn bộ cài đặt hiện tại. */
void save(const AppSettings& settings);

} // namespace EZTranslator::SettingsStore
