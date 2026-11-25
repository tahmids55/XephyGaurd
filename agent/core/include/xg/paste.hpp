#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "xg/types.hpp"

namespace xg {

/// Reduces clipboard text to numeric features. Called on the PC; the caller must discard the text
/// immediately afterwards. Nothing derived from content other than these numbers leaves this function.
PasteInfo analyze_clipboard_text(std::string_view text);

/// 0..1 heuristic score that text is source code (braces, semicolons, indentation, keywords...).
double code_likeness(std::string_view text);

/// Focused window as the helper last saw it. `file` is set only with title_capture=full (I-AGENT-9).
struct FocusLabel {
    std::string app;
    std::string file;
};

/// The last measured copy, kept in memory by the helper so a paste can be attributed to it.
struct CopyRecord {
    std::string source;  // "app" or "app (file)"
    uint64_t chars = 0;
    uint64_t lines = 0;
    bool looks_like_code = false;
    double code_score = 0.0;
    int64_t timestamp_ms = 0;
};

/// "app (file)" when file is set, else app.
std::string label_app(const std::string& app, const std::string& file);

/// Source label of a copy. `pi.source_app` is the selection owner; the focused window's file is attached only
/// when the owner is that same app (never "chrome (main.cpp)" from an editor that merely had focus).
std::string resolve_copy_source(const PasteInfo& pi, const FocusLabel& focus);

/// True when a copy is only a clipboard manager re-owning the previous copy (same size, a different owner,
/// within `window_ms`): the helper then keeps the original source and sends no second copy event.
bool is_clipboard_reown(const PasteInfo& pi, const CopyRecord& last, int64_t now_ms, int64_t window_ms = 2000);

struct PasteRoute {
    std::string src;
    std::string dst;
};

/// Where a paste came from and went to. `pi.target_app` (focused at paste time) wins over `focus`, which can lag.
/// The source is the last copy when its size matches what is on the clipboard; otherwise the clipboard owner at
/// paste time; otherwise empty (unknown), never an older, unrelated copy.
PasteRoute resolve_paste_route(const PasteInfo& pi, const FocusLabel& focus, const CopyRecord& last);

}  // namespace xg
