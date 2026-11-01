// Scans installed IDE / editor / browser extensions for every local user.
#include <filesystem>

#include "linux_common.hpp"
#include "xg/util.hpp"

namespace xg::lnx {
namespace {
namespace fs = std::filesystem;

/// "github.copilot-chat-0.22.4" -> {"github.copilot-chat", "0.22.4"}; also handles "-linux-x64" suffixes.
std::pair<std::string, std::string> split_vsix_dir(const std::string& dir) {
    for (size_t i = dir.size(); i-- > 0;)
        if (dir[i] == '-' && i + 1 < dir.size() && std::isdigit(static_cast<unsigned char>(dir[i + 1]))) {
            // keep searching left if what precedes is also a version segment like "-1.2.3-linux-x64"
            auto left = dir.substr(0, i);
            if (left.find('.') != std::string::npos) return {to_lower(left), dir.substr(i + 1)};
        }
    return {to_lower(dir), ""};
}

class ExtensionScanner final : public IExtensionScanner {
public:
    explicit ExtensionScanner(ISystemInfo* sys) : sys_(sys) {}

    std::vector<ExtensionInfo> scan() override {
        std::vector<ExtensionInfo> out;
        std::vector<std::pair<std::string, std::string>> homes;  // user, home
        for (const auto& u : list_dir("/home")) homes.emplace_back(u, "/home/" + u);
        homes.emplace_back("root", "/root");
        // Shared, root-owned extension directory recommended for labs (code --extensions-dir).
        scan_vscode_dir("/opt/xephyguard/vscode-extensions", "vscode", "(shared)", out);
        for (const auto& [user, home] : homes) {
            scan_vscode_dir(home + "/.vscode/extensions", "vscode", user, out);
            scan_vscode_dir(home + "/.vscode-insiders/extensions", "vscode-insiders", user, out);
            scan_vscode_dir(home + "/.vscode-oss/extensions", "vscodium", user, out);
            scan_vscode_dir(home + "/.vscode-server/extensions", "vscode", user, out);
            scan_vscode_dir(home + "/.cursor/extensions", "cursor", user, out);
            scan_vscode_dir(home + "/.windsurf/extensions", "windsurf", user, out);
            // JetBrains: ~/.local/share/JetBrains/<Product><ver>/<plugin>
            for (const auto& prod : list_dir(home + "/.local/share/JetBrains"))
                for (const auto& plugin : list_dir(home + "/.local/share/JetBrains/" + prod))
                    out.push_back({"jetbrains", to_lower(plugin), prod, user, home + "/.local/share/JetBrains/" + prod});
            // Chromium family: <profile>/Extensions/<id>/<version>
            const std::pair<const char*, const char*> chromes[] = {
                {"chrome", "/.config/google-chrome"}, {"chromium", "/.config/chromium"},
                {"edge", "/.config/microsoft-edge"}, {"brave", "/.config/BraveSoftware/Brave-Browser"}};
            for (const auto& [ide, rel] : chromes)
                for (const auto& profile : list_dir(home + rel)) {
                    const std::string ext = home + rel + "/" + profile + "/Extensions";
                    for (const auto& id : list_dir(ext)) {
                        auto vers = list_dir(ext + "/" + id);
                        out.push_back({ide, to_lower(id), vers.empty() ? "" : vers.front(), user, ext});
                    }
                }
            // Firefox: <profile>/extensions/<id>.xpi
            for (const auto& profile : list_dir(home + "/.mozilla/firefox")) {
                const std::string ext = home + "/.mozilla/firefox/" + profile + "/extensions";
                for (const auto& f : list_dir(ext)) {
                    auto id = f.ends_with(".xpi") ? f.substr(0, f.size() - 4) : f;
                    out.push_back({"firefox", to_lower(id), "", user, ext});
                }
            }
        }
        return out;
    }

private:
    static void scan_vscode_dir(const std::string& dir, const std::string& ide, const std::string& user,
                                std::vector<ExtensionInfo>& out) {
        for (const auto& d : list_dir(dir)) {
            if (d == "extensions.json" || d.starts_with(".")) continue;
            auto [id, ver] = split_vsix_dir(d);
            out.push_back({ide, id, ver, user, dir});
        }
    }
    ISystemInfo* sys_;
};

}  // namespace

std::unique_ptr<IExtensionScanner> make_extension_scanner(ISystemInfo* sys) {
    return std::make_unique<ExtensionScanner>(sys);
}

}  // namespace xg::lnx
