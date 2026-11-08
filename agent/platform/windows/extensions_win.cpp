// Installed IDE/editor/browser extensions for every profile under C:\Users.
// Live usage detection is driven by kernel per-socket AI traffic (OS DNS + TLS SNI + EStats).
#include <filesystem>
#include <set>

#include "win_common.hpp"
#include "xg/util.hpp"

namespace xg::win {
namespace {
namespace fs = std::filesystem;

std::vector<std::string> ls(const fs::path& p) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(p, ec)) return out;
    for (auto& e : fs::directory_iterator(p, fs::directory_options::skip_permission_denied, ec))
        out.push_back(e.path().filename().string());
    return out;
}

std::pair<std::string, std::string> split_vsix_dir(const std::string& dir) {
    for (size_t i = dir.size(); i-- > 0;)
        if (dir[i] == '-' && i + 1 < dir.size() && std::isdigit(static_cast<unsigned char>(dir[i + 1]))) {
            auto left = dir.substr(0, i);
            if (left.find('.') != std::string::npos) return {to_lower(left), dir.substr(i + 1)};
        }
    return {to_lower(dir), ""};
}

class ExtensionScanner final : public IExtensionScanner {
public:
    std::vector<ExtensionInfo> scan() override {
        std::vector<ExtensionInfo> out;
        const fs::path users = profiles_dir();
        auto vsc = [&](const fs::path& dir, const char* ide, const std::string& user) {
            for (const auto& d : ls(dir)) {
                if (d == "extensions.json" || d.starts_with(".")) continue;
                auto [id, ver] = split_vsix_dir(d);
                out.push_back({ide, id, ver, user, (dir / d).string(), false, ""});
            }
        };
        vsc("C:\\ProgramData\\XephyGuard\\vscode-extensions", "vscode", "(shared)");
        for (const auto& user : ls(users)) {
            if (user == "Public" || user == "Default" || user == "Default User" || user == "All Users") continue;
            const fs::path home = users / user;
            const fs::path roaming = home / "AppData" / "Roaming";
            const fs::path local = home / "AppData" / "Local";
            vsc(home / ".vscode" / "extensions", "vscode", user);
            vsc(home / ".vscode-insiders" / "extensions", "vscode-insiders", user);
            vsc(home / ".vscode-oss" / "extensions", "vscodium", user);
            vsc(home / ".cursor" / "extensions", "cursor", user);
            vsc(home / ".windsurf" / "extensions", "windsurf", user);
            for (const auto& prod : ls(roaming / "JetBrains")) {
                std::set<std::string> disabled;
                bool ok = false;
                for (const auto& line : split(read_file((roaming / "JetBrains" / prod / "disabled_plugins.txt").string(), &ok), '\n')) {
                    auto t = to_lower(trim(line));
                    if (!t.empty()) disabled.insert(t);
                }
                for (const auto& plugin : ls(roaming / "JetBrains" / prod / "plugins")) {
                    auto pid = to_lower(plugin);
                    if (disabled.contains(pid)) continue;
                    out.push_back({"jetbrains", pid, prod, user, (roaming / "JetBrains" / prod / "plugins" / plugin).string()});
                }
            }
            const std::pair<const char*, fs::path> chromes[] = {
                {"chrome", local / "Google" / "Chrome" / "User Data"},
                {"edge", local / "Microsoft" / "Edge" / "User Data"},
                {"brave", local / "BraveSoftware" / "Brave-Browser" / "User Data"}};
            for (const auto& [ide, root] : chromes)
                for (const auto& profile : ls(root)) {
                    const auto ext = root / profile / "Extensions";
                    for (const auto& id : ls(ext)) {
                        auto vers = ls(ext / id);
                        out.push_back({ide, to_lower(id), vers.empty() ? "" : vers.front(), user, ext.string(), false, ""});
                    }
                }
            for (const auto& profile : ls(roaming / "Mozilla" / "Firefox" / "Profiles"))
                for (const auto& f : ls(roaming / "Mozilla" / "Firefox" / "Profiles" / profile / "extensions"))
                    out.push_back({"firefox", to_lower(f.ends_with(".xpi") ? f.substr(0, f.size() - 4) : f), "", user, ""});
        }
        return out;
    }
};

}  // namespace

std::unique_ptr<IExtensionScanner> make_extension_scanner() { return std::make_unique<ExtensionScanner>(); }

}  // namespace xg::win
