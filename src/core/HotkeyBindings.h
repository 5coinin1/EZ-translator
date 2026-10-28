#pragma once

#include "core/Types.h"

namespace EZTranslator {

struct HotkeyBindingDefinition
{
    const char* id;
    const char* settingsKey;
    const char* label;
    QString AppSettings::* value;
};

inline constexpr HotkeyBindingDefinition hotkeyBindings[] = {
    {"toggle", "toggleTranslationHotkey", "Bắt đầu / Dừng dịch", &AppSettings::toggleTranslationHotkey},
    {"overlay", "toggleOverlayHotkey", "Bật / Ẩn overlay", &AppSettings::toggleOverlayHotkey},
    {"chatBox", "toggleChatBoxHotkey", "Bật / Ẩn cửa sổ bản dịch", &AppSettings::toggleChatBoxHotkey},
    {"selectRegion", "selectRegionHotkey", "Chọn vùng dịch", &AppSettings::selectRegionHotkey},
    {"settings", "openSettingsHotkey", "Mở Cài đặt", &AppSettings::openSettingsHotkey},
    {"log", "openLogHotkey", "Mở log", &AppSettings::openLogHotkey},
    {"clearRegions", "clearRegionsHotkey", "Xoá mọi vùng dịch", &AppSettings::clearRegionsHotkey},
    {"clearSelectedRegion", "clearSelectedRegionHotkey", "Xoá vùng đang chọn", &AppSettings::clearSelectedRegionHotkey},
    {"showRegion", "showRegionHotkey", "Hiện / Ẩn khung vùng", &AppSettings::showRegionHotkey},
    {"swapLanguages", "swapLanguagesHotkey", "Đổi ngôn ngữ nguồn và đích", &AppSettings::swapLanguagesHotkey},
    {"retryTranslation", "retryTranslationHotkey", "Dịch lại nội dung hiện tại", &AppSettings::retryTranslationHotkey},
};

} // namespace EZTranslator
