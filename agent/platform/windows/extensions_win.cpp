// Installed IDE/editor/browser extensions for every profile under C:\Users,
// plus live activity detection via recent globalStorage / state writes.
#include <chrono>
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

// Returns true if any file inside `dir` (up to depth 2) was modified within the last `max_age_s` seconds.
bool recently_modified(const fs::path& dir, int max_age_s = 30) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return false;
    const auto now = fs::file_time_type::clock::now();
    const auto cutoff = now - std::chrono::seconds(max_age_s);
    auto check_entry = [&](const fs::path& p) {
        std::error_code tec;
        auto wt = fs::last_write_time(p, tec);
        return !tec && wt >= cutoff;
    };
    if (check_entry(dir)) return true;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); ++it) {
        if (it.depth() > 2) {
            it.disable_recursion_pending();
            continue;
        }
        if (check_entry(it->path())) return true;
    }
    return false;
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
        auto vsc = [&](const fs::path& dir, const fs::path& global_storage, const char* ide, const std::string& user) {
            for (const auto& d : ls(dir)) {
                if (d == "extensions.json" || d.starts_with(".")) continue;
                auto [id, ver] = split_vsix_dir(d);
                bool active = !global_storage.empty() && recently_modified(global_storage / id, 30);
                out.push_back({ide, id, ver, user, (dir / d).string(), active, active ? "recent storage activity" : ""});
            }
        };
        vsc("C:\\ProgramData\\XephyGuard\\vscode-extensions", {}, "vscode", "(shared)");
        for (const auto& user : ls(users)) {
            if (user == "Public" || user == "Default" || user == "Default User" || user == "All Users") continue;
            const fs::path home = users / user;
            const fs::path roaming = home / "AppData" / "Roaming";
            const fs::path local = home / "AppData" / "Local";
            vsc(home / ".vscode" / "extensions", roaming / "Code" / "User" / "globalStorage", "vscode", user);
            vsc(home / ".vscode-insiders" / "extensions", roaming / "Code - Insiders" / "User" / "globalStorage", "vscode-insiders", user);
            vsc(home / ".vscode-oss" / "extensions", roaming / "VSCodium" / "User" / "globalStorage", "vscodium", user);
            vsc(home / ".cursor" / "extensions", roaming / "Cursor" / "User" / "globalStorage", "cursor", user);
            vsc(home / ".windsurf" / "extensions", roaming / "Windsurf" / "User" / "globalStorage", "windsurf", user);
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
                        const bool active = recently_modified(root / profile / "Local Extension Settings" / id, 30);
                        out.push_back({ide, to_lower(id), vers.empty() ? "" : vers.front(), user, ext.string(),
                                       active, active ? "recent extension storage activity" : ""});
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
