#pragma once
#include <string>
#include <vector>

// One indexed MIDI file. Lightweight metadata captured at scan time so the
// browse UI doesn't have to re-parse files on every click.
struct LibraryEntry {
    std::string path;          // absolute path on disk
    std::string title;         // basename without extension; user-renamable later
    double      duration = 0;  // seconds, post-doTimeAnalysis
    int         noteCount = 0; // NoteOn count with velocity > 0
    int         trackCount = 0;
    bool        loadFailed = false;  // midifile couldn't parse
};

// On-disk catalog of MIDI files under a configurable root directory.
// Stage 3a MVP: in-memory only, re-scans on demand. Caching to disk and
// async scan are deferred polish.
class MidiLibrary {
public:
    // Recursively scan `root` for .mid / .midi files, parse each with
    // midifile, populate entries. Replaces current contents. Synchronous.
    // Returns the number of entries successfully indexed (loadFailed
    // entries still count and are kept so the UI can show them as broken).
    int scanDirectory(const std::string& root);

    // Case-insensitive substring search over titles. Empty query returns
    // every entry. Result is a list of indices into entries().
    std::vector<std::size_t> search(const std::string& query) const;

    const std::vector<LibraryEntry>& entries() const { return entries_; }
    const std::string& currentRoot() const { return root_; }

private:
    std::string root_;
    std::vector<LibraryEntry> entries_;
};
