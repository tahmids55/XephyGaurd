#include "xg/paste.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace xg {

double code_likeness(std::string_view t) {
    if (t.size() < 8) return 0.0;
    size_t lines = 1, indented = 0, sym = 0, alpha = 0, line_end_semis = 0, line_end_braces = 0;
    bool line_start = true;
    char prev = 0;
    for (char c : t) {
        if (c == '\n') {
            ++lines;
            if (prev == ';') ++line_end_semis;
            if (prev == '{' || prev == '}' || prev == ':') ++line_end_braces;
            line_start = true;
            prev = c;
            continue;
        }
        if (line_start && (c == ' ' || c == '\t')) { ++indented; line_start = false; }
        else if (line_start && c != '\r') line_start = false;
        if (std::string_view("{}()[];=<>+-*/&|!%#:").find(c) != std::string_view::npos) ++sym;
        if (std::isalpha(static_cast<unsigned char>(c))) ++alpha;
        if (c != ' ' && c != '\r' && c != '\t') prev = c;
    }
    static constexpr std::array<std::string_view, 28> kw = {
        "def ", "return", "class ", "import ", "#include", "int ", "void ", "for (", "for(", "while", "if (", "if(",
        "else", "public ", "private ", "static ", "const ", "function", "=>", "println", "printf", "System.out",
        "std::", "self.", "elif ", "lambda", "struct ", "#define"};
    size_t kw_hits = 0;
    for (auto k : kw) if (t.find(k) != std::string_view::npos) ++kw_hits;

    const double n = double(t.size());
    double score = 0;
    score += std::min(1.0, (double(sym) / n) / 0.08) * 0.30;                 // symbol density
    score += std::min(1.0, double(kw_hits) / 4.0) * 0.30;                     // keywords
    score += (lines > 1 ? std::min(1.0, double(indented) / double(lines) / 0.4) : 0.0) * 0.20;   // indentation
    score += (lines > 1 ? std::min(1.0, double(line_end_semis + line_end_braces) / double(lines) / 0.4) : 0.0) * 0.20;
    if (alpha == 0) score *= 0.5;
    return std::clamp(score, 0.0, 1.0);
}

PasteInfo analyze_clipboard_text(std::string_view text) {
    PasteInfo p;
    // Count characters as UTF-8 code points.
    for (unsigned char c : text) if ((c & 0xC0) != 0x80) ++p.chars;
    p.lines = text.empty() ? 0 : uint64_t(std::count(text.begin(), text.end(), '\n') + 1);
    p.code_score = code_likeness(text.substr(0, std::min<size_t>(text.size(), 64 * 1024)));
    p.looks_like_code = p.code_score >= 0.5;
    return p;
}

}  // namespace xg
