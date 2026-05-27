#pragma once
#include <map>
#include <string>

// Tiny tab-separated key=value store used for string settings that don't
// fit the existing int-only SettingsHandler. Currently used for:
//   - library_root   absolute path of the MIDI library directory
//   - target_app_mac last-selected app name for macOS keystroke targeting
//
// File layout: one "key\tvalue" pair per line. Created lazily; missing file
// is treated as "no settings yet" with no error.
class AppConfig {
public:
    explicit AppConfig(std::string path);

    void load();
    bool save() const;

    std::string get(const std::string& key, const std::string& def = "") const;
    void set(const std::string& key, const std::string& value);
    bool has(const std::string& key) const;

    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::map<std::string, std::string> kv_;
};
