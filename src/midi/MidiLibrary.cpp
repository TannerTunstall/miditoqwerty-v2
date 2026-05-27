#include "MidiLibrary.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "MidiFile.h"

namespace fs = std::filesystem;

namespace {

std::string toLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool isMidiExt(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = toLower(path.substr(dot));
    return ext == ".mid" || ext == ".midi";
}

// Cache-invalidation sentinel. The unit is opaque (file_clock ticks in C++17),
// but identical for the same on-disk state and different after any
// modification — which is all the cache needs for a hit/miss decision.
std::int64_t fileMtime(const fs::path& p) {
    std::error_code ec;
    auto ft = fs::last_write_time(p, ec);
    if (ec) return 0;
    return ft.time_since_epoch().count();
}

std::int64_t fileSize(const fs::path& p) {
    std::error_code ec;
    auto sz = fs::file_size(p, ec);
    return ec ? 0 : (std::int64_t)sz;
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

// Try-parse helpers that don't throw on malformed cache lines.
double parseDouble(const std::string& s, double def = 0.0) {
    try { return std::stod(s); } catch (...) { return def; }
}
long long parseLL(const std::string& s, long long def = 0) {
    try { return std::stoll(s); } catch (...) { return def; }
}

}  // namespace

MidiLibrary::MidiLibrary() = default;

MidiLibrary::~MidiLibrary() {
    cancelRequested.store(true);
    if (worker_.joinable()) worker_.join();
}

void MidiLibrary::setCachePath(const std::string& p) {
    cachePath_ = p;
    loadCache();
}

void MidiLibrary::loadCache() {
    cache_.clear();
    if (cachePath_.empty()) return;
    std::ifstream in(cachePath_);
    if (!in) return;
    std::string line;
    // Header: VERSION\t1
    std::getline(in, line);  // discard

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto f = splitTabs(line);
        if (f.size() < 8 || f[0] != "ENTRY") continue;
        LibraryEntry e;
        e.path       = f[1];
        e.title      = f[2];
        e.duration   = parseDouble(f[3]);
        e.noteCount  = (int)parseLL(f[4]);
        e.trackCount = (int)parseLL(f[5]);
        e.mtime      = parseLL(f[6]);
        e.fileSize   = parseLL(f[7]);
        cache_.push_back(std::move(e));
    }
}

void MidiLibrary::saveCache() const {
    if (cachePath_.empty()) return;
    std::error_code ec;
    fs::create_directories(fs::path(cachePath_).parent_path(), ec);
    std::ofstream out(cachePath_, std::ios::trunc);
    if (!out) return;
    out << "VERSION\t1\n";
    for (const auto& e : entries_) {
        out << "ENTRY\t" << e.path
            << "\t" << e.title
            << "\t" << e.duration
            << "\t" << e.noteCount
            << "\t" << e.trackCount
            << "\t" << e.mtime
            << "\t" << e.fileSize
            << "\n";
    }
}

int MidiLibrary::scanDirectory(const std::string& root) {
    doScan(root);
    return (int)entries_.size();
}

void MidiLibrary::scanDirectoryAsync(const std::string& root) {
    cancelRequested.store(true);
    if (worker_.joinable()) worker_.join();
    cancelRequested.store(false);
    scanning_.store(true);
    progressDone_.store(0);
    progressTotal_.store(0);

    worker_ = std::thread([this, root]() {
        doScan(root);
        scanning_.store(false);
    });
}

void MidiLibrary::doScan(std::string root) {
    std::vector<LibraryEntry> next;
    next.reserve(cache_.size() + 16);

    // Build a quick lookup of cached entries by path so we can skip the
    // (expensive) midifile parse for files that haven't changed.
    std::map<std::string, const LibraryEntry*> cacheByPath;
    for (const auto& e : cache_) cacheByPath[e.path] = &e;

    if (!root.empty() && fs::exists(root) && fs::is_directory(root)) {
        // First pass: enumerate to get total count for progress.
        std::vector<fs::path> midiPaths;
        std::error_code ec;
        for (const auto& p : fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (!p.is_regular_file()) continue;
            if (!isMidiExt(p.path().string())) continue;
            midiPaths.push_back(p.path());
        }
        progressTotal_.store(midiPaths.size());

        for (const auto& path : midiPaths) {
            if (cancelRequested.load()) break;

            LibraryEntry e;
            e.path     = path.string();
            e.title    = path.stem().string();
            e.mtime    = fileMtime(path);
            e.fileSize = fileSize(path);

            // Cache hit: same mtime + size → reuse cached metadata.
            auto cached = cacheByPath.find(e.path);
            if (cached != cacheByPath.end()
                && cached->second->mtime    == e.mtime
                && cached->second->fileSize == e.fileSize) {
                e.duration   = cached->second->duration;
                e.noteCount  = cached->second->noteCount;
                e.trackCount = cached->second->trackCount;
                e.loadFailed = cached->second->loadFailed;
            } else {
                // Re-parse via midifile.
                smf::MidiFile mf;
                if (mf.read(e.path)) {
                    mf.doTimeAnalysis();
                    e.trackCount = mf.getTrackCount();
                    for (int t = 0; t < mf.getTrackCount(); ++t) {
                        for (int i = 0; i < mf[t].size(); ++i) {
                            auto& ev = mf[t][i];
                            if (ev.isNoteOn() && ev.size() > 2 && (int)ev[2] > 0) {
                                ++e.noteCount;
                            }
                            if (ev.seconds > e.duration) e.duration = ev.seconds;
                        }
                    }
                } else {
                    e.loadFailed = true;
                }
            }

            next.push_back(std::move(e));
            progressDone_.fetch_add(1);
        }
    }

    std::sort(next.begin(), next.end(),
              [](const LibraryEntry& a, const LibraryEntry& b) {
                  return toLower(a.title) < toLower(b.title);
              });

    {
        std::lock_guard<std::mutex> lk(mutex_);
        root_ = root;
        entries_ = std::move(next);
    }
    // Persist updated cache. Best-effort; ignores write failures.
    saveCache();
}

std::vector<std::size_t> MidiLibrary::search(const std::string& query) const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<std::size_t> result;
    if (query.empty()) {
        result.reserve(entries_.size());
        for (std::size_t i = 0; i < entries_.size(); ++i) result.push_back(i);
        return result;
    }
    const std::string q = toLower(query);
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (toLower(entries_[i].title).find(q) != std::string::npos) {
            result.push_back(i);
        }
    }
    return result;
}

std::vector<LibraryEntry> MidiLibrary::snapshot() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return entries_;
}
