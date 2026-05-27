#pragma once
#include <string>
#include <vector>

// One slot in a playlist. The libraryPath is the source-of-truth identifier;
// title is cached for display so we don't need a Library lookup every frame.
// Per-entry overrides multiply the player's live tempo/velocity scale when
// this entry is the active one.
struct PlaylistEntry {
    std::string libraryPath;
    std::string title;             // cached at add-time; resync on rescan
    float       tempoOverride    = 1.0f;
    float       velocityOverride = 1.0f;
};

struct Playlist {
    std::string name;
    std::vector<PlaylistEntry> entries;
};

// On-disk catalog of named playlists. One file per playlist under rootDir.
// Persistence format is the same TSV-ish style as settings.dat — a one-line
// header followed by ENTRY rows. Keeps us from pulling in a JSON dep just
// for this.
class PlaylistManager {
public:
    explicit PlaylistManager(std::string rootDir);

    // Re-read every *.playlist file under rootDir into memory. Replaces
    // current contents. Silently tolerates a missing rootDir (returns 0).
    int load();

    // Write a playlist out to <rootDir>/<name>.playlist. Creates rootDir
    // if it doesn't exist. Returns false on filesystem failure.
    bool save(const Playlist& pl);

    // Delete a playlist (from disk + memory). Returns false if not found.
    bool remove(const std::string& name);

    // Create an empty playlist, append to in-memory list, and persist.
    // No-op + returns nullptr if `name` is empty or already exists.
    Playlist* createNew(const std::string& name);

    std::vector<Playlist>& playlists()             { return playlists_; }
    const std::vector<Playlist>& playlists() const { return playlists_; }
    Playlist* find(const std::string& name);
    const std::string& root() const { return rootDir; }

private:
    std::string rootDir;
    std::vector<Playlist> playlists_;
};
