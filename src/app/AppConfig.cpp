#include "AppConfig.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

AppConfig::AppConfig(std::string p) : path_(std::move(p)) {}

void AppConfig::load() {
    kv_.clear();
    std::ifstream in(path_);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        kv_[line.substr(0, tab)] = line.substr(tab + 1);
    }
}

bool AppConfig::save() const {
    if (path_.empty()) return false;
    std::error_code ec;
    fs::create_directories(fs::path(path_).parent_path(), ec);
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return false;
    for (auto& kv : kv_) {
        out << kv.first << "\t" << kv.second << "\n";
    }
    return true;
}

std::string AppConfig::get(const std::string& key, const std::string& def) const {
    auto it = kv_.find(key);
    return it == kv_.end() ? def : it->second;
}

void AppConfig::set(const std::string& key, const std::string& value) {
    kv_[key] = value;
}

bool AppConfig::has(const std::string& key) const {
    return kv_.find(key) != kv_.end();
}
