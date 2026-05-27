#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// One indexed MIDI file. Lightweight metadata captured at scan time so the
// browse UI doesn't have to re-parse files on every click. mtime + size are
// stored so a subsequent scan can skip the (slow) midifile parse for files
// that haven't changed since last index.
struct LibraryEntry {
    std::string path;            // absolute path on disk
    std::string title;           // basename without extension
    double      duration = 0;    // seconds, post-doTimeAnalysis
    int         noteCount = 0;   // NoteOn count with velocity > 0
    int         trackCount = 0;
    bool        loadFailed = false;
    std::int64_t mtime = 0;      // last-modified unix time
    std::int64_t fileSize = 0;   // bytes
};

// On-disk catalog of MIDI files under a configurable root directory.
// - Scan results are persisted to a cache file so cold-start doesn't re-parse
//   every .mid (parsing a 6,000-event file via midifile takes ~50 ms).
// - Async scan keeps the UI responsive; isScanning() / scanProgress() let
//   the UI render a progress bar.
class MidiLibrary {
public:
    MidiLibrary();
    ~MidiLibrary();

    // Set where the cache file lives. Optional; if empty, scan results are
    // in-memory only.
    void setCachePath(const std::string& path);

    // Recursively scan `root` for .mid / .midi files. Synchronous variant
    // blocks until done. Reuses cached entries when mtime + size match,
    // re-parses everything else. Returns total entries (incl. loadFailed).
    int scanDirectory(const std::string& root);

    // Same as scanDirectory but returns immediately. Background thread writes
    // into entries_ under the internal mutex when complete. Aborts/joins any
    // in-flight scan before starting the new one.
    void scanDirectoryAsync(const std::string& root);

    bool        isScanning()  const { return scanning_.load(); }
    std::size_t scanProgress() const { return progressDone_.load(); }
    std::size_t scanTotal()    const { return progressTotal_.load(); }

    // Case-insensitive substring search over titles. Empty query returns
    // every entry. Result is a list of indices into entries().
    // Safe to call from any thread (internal lock).
    std::vector<std::size_t> search(const std::string& query) const;

    // Copy-out for the UI. Safe under the lock and decoupled from
    // the background thread mutating entries_.
    std::vector<LibraryEntry> snapshot() const;

    // For backward compat / unlocked access where the caller knows no scan
    // is in flight.
    const std::vector<LibraryEntry>& entries() const { return entries_; }
    const std::string& currentRoot() const { return root_; }

private:
    void doScan(std::string root);   // worker body, runs on any thread
    void loadCache();                // populates cache_ from cachePath_
    void saveCache() const;          // serialises entries_ to cachePath_

    std::string root_;
    std::string cachePath_;
    std::vector<LibraryEntry> entries_;
    std::vector<LibraryEntry> cache_; // last-known disk state

    mutable std::mutex     mutex_;
    std::thread            worker_;
    std::atomic<bool>      scanning_      {false};
    std::atomic<bool>      cancelRequested{false};
    std::atomic<std::size_t> progressDone_ {0};
    std::atomic<std::size_t> progressTotal_{0};
};
