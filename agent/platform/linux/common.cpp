#include <dirent.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <sstream>

#include "linux_common.hpp"

namespace xg::lnx {

std::string read_small(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string read_link(const std::string& path) {
    char buf[4096];
    ssize_t n = ::readlink(path.c_str(), buf, sizeof buf - 1);
    if (n <= 0) return {};
    return std::string(buf, size_t(n));
}

std::vector<std::string> list_dir(const std::string& path) {
    std::vector<std::string> out;
    DIR* d = ::opendir(path.c_str());
    if (!d) return out;
    while (dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.' && (e->d_name[1] == 0 || (e->d_name[1] == '.' && e->d_name[2] == 0))) continue;
        out.emplace_back(e->d_name);
    }
    ::closedir(d);
    return out;
}

std::string user_name(uint32_t uid) {
    passwd pw{}, *res = nullptr;
    char buf[4096];
    if (getpwuid_r(uid, &pw, buf, sizeof buf, &res) == 0 && res) return pw.pw_name;
    return std::to_string(uid);
}

std::string user_home(const std::string& user) {
    passwd pw{}, *res = nullptr;
    char buf[4096];
    if (getpwnam_r(user.c_str(), &pw, buf, sizeof buf, &res) == 0 && res) return pw.pw_dir;
    return {};
}

uint32_t file_uid(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 ? st.st_uid : 0;
}

}  // namespace xg::lnx
