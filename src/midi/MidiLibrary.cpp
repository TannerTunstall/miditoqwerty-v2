#include "MidiLibrary.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

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

}  // namespace

int MidiLibrary::scanDirectory(const std::string& root) {
    entries_.clear();
    root_ = root;
    if (root.empty()) return 0;
    if (!fs::exists(root) || !fs::is_directory(root)) return 0;

    std::error_code ec;
    auto iter = fs::recursive_directory_iterator(root,
        fs::directory_options::skip_permission_denied, ec);
    if (ec) return 0;

    for (const auto& p : iter) {
        if (!p.is_regular_file()) continue;
        if (!isMidiExt(p.path().string())) continue;

        LibraryEntry e;
        e.path  = p.path().string();
        e.title = p.path().stem().string();

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

        entries_.push_back(std::move(e));
    }

    std::sort(entries_.begin(), entries_.end(),
              [](const LibraryEntry& a, const LibraryEntry& b) {
                  return toLower(a.title) < toLower(b.title);
              });

    return (int)entries_.size();
}

std::vector<std::size_t> MidiLibrary::search(const std::string& query) const {
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
