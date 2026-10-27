#pragma once

namespace EZTranslator {

/** Số intra-op thread dành cho engine dịch offline trên CPU. */
[[nodiscard]] int chooseTranslationThreads();

} // namespace EZTranslator
