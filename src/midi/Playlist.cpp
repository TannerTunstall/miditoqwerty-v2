#include "Playlist.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// Replace any character that's not safe in a filename. Keeps the playlist
// name human-readable while making the on-disk file simple.
std::string sanitizeName(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c == '\t' ||
            c == '\n' || c == '\r') {
            out.push_back('_');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { out.push_back(cur); cur.clear(); }
        else            { cur.push_back(c); }
    }
    out.push_back(cur);
    return out;
}

}  // namespace

PlaylistManager::PlaylistManager(std::string root) : rootDir(std::move(root)) {}

int PlaylistManager::load() {
    playlists_.clear();
    if (rootDir.empty()) return 0;
    std::error_code ec;
    if (!fs::exists(rootDir, ec) || !fs::is_directory(rootDir, ec)) return 0;

    for (const auto& p : fs::directory_iterator(rootDir, ec)) {
        if (ec) break;
        if (!p.is_regular_file()) continue;
        if (p.path().extension() != ".playlist") continue;

        std::ifstream in(p.path());
        if (!in) continue;
        Playlist pl;
        std::string line;
        while (std::getline(in, line)) {
            // Strip trailing \r if the file came from Windows.
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            auto fields = splitTabs(line);
            if (fields.empty()) continue;
            if (fields[0] == "NAME" && fields.size() >= 2) {
                pl.name = fields[1];
            } else if (fields[0] == "ENTRY" && fields.size() >= 2) {
                PlaylistEntry e;
                e.libraryPath = fields[1];
                if (fields.size() >= 3) e.title = fields[2];
                if (fields.size() >= 4) e.tempoOverride    = std::stof(fields[3]);
                if (fields.size() >= 5) e.velocityOverride = std::stof(fields[4]);
                pl.entries.push_back(std::move(e));
            }
        }
        if (pl.name.empty()) {
            pl.name = p.path().stem().string();
        }
        playlists_.push_back(std::move(pl));
    }

    std::sort(playlists_.begin(), playlists_.end(),
              [](const Playlist& a, const Playlist& b){ return a.name < b.name; });
    return (int)playlists_.size();
}

bool PlaylistManager::save(const Playlist& pl) {
    if (pl.name.empty() || rootDir.empty()) return false;
    std::error_code ec;
    fs::create_directories(rootDir, ec);
    if (ec) return false;

    fs::path file = fs::path(rootDir) / (sanitizeName(pl.name) + ".playlist");
    std::ofstream out(file, std::ios::trunc);
    if (!out) return false;

    out << "NAME\t" << pl.name << "\n";
    for (const auto& e : pl.entries) {
        out << "ENTRY\t" << e.libraryPath
            << "\t" << e.title
            << "\t" << e.tempoOverride
            << "\t" << e.velocityOverride
            << "\n";
    }
    return true;
}

bool PlaylistManager::remove(const std::string& name) {
    auto it = std::find_if(playlists_.begin(), playlists_.end(),
                           [&](const Playlist& p){ return p.name == name; });
    if (it == playlists_.end()) return false;
    playlists_.erase(it);
    if (!rootDir.empty()) {
        fs::path file = fs::path(rootDir) / (sanitizeName(name) + ".playlist");
        std::error_code ec;
        fs::remove(file, ec);  // tolerate missing
    }
    return true;
}

Playlist* PlaylistManager::createNew(const std::string& name) {
    if (name.empty()) return nullptr;
    if (find(name) != nullptr) return nullptr;
    Playlist pl;
    pl.name = name;
    playlists_.push_back(std::move(pl));
    Playlist* created = &playlists_.back();
    save(*created);
    return created;
}

Playlist* PlaylistManager::find(const std::string& name) {
    for (auto& p : playlists_) if (p.name == name) return &p;
    return nullptr;
}
