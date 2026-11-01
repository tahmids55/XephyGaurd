#pragma once
#include <string_view>

#include "xg/types.hpp"

namespace xg {

/// Reduces clipboard text to numeric features. Called on the PC; the caller must discard the text
/// immediately afterwards. Nothing derived from content other than these numbers leaves this function.
PasteInfo analyze_clipboard_text(std::string_view text);

/// 0..1 heuristic score that text is source code (braces, semicolons, indentation, keywords...).
double code_likeness(std::string_view text);

}  // namespace xg
