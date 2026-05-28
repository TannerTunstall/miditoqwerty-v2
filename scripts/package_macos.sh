#!/usr/bin/env bash
# Wrap a built miditoqwerty binary into a self-contained macOS .app bundle.
# Copies the SDL2 / PortMidi dylibs it links against into the bundle's
# Frameworks directory and rewrites the binary's install names so the .app
# runs on a machine without Homebrew (or with a differently-located Homebrew).
#
# Compatible with the bash 3.2 that ships with macOS — no `mapfile`, no
# `declare -A`. Uses plain arrays + a ":"-delimited seen-string.
#
# Usage: package_macos.sh <binary-path> <output-app-path> [version-string]

set -euo pipefail

BIN="${1:?usage: package_macos.sh <binary> <output.app> [version]}"
APP="${2:?usage: package_macos.sh <binary> <output.app> [version]}"
VERSION="${3:-dev}"

if [ ! -x "$BIN" ]; then
    echo "no executable at $BIN" >&2
    exit 1
fi

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/miditoqwerty"
chmod +x "$APP/Contents/MacOS/miditoqwerty"

# Print every external dylib `target` references, excluding system libs and
# anything already living at @executable_path. One per line.
collect() {
    local target="$1"
    otool -L "$target" 2>/dev/null \
        | awk 'NR>1 {print $1}' \
        | grep -Ev '^(/System|/usr/lib|@executable_path|@rpath/libobjc|@rpath/libSystem)' \
        | grep -v "$(basename "$target")\$" \
        || true
}

# Queue and seen-string (":dep1:dep2:..." sentinels avoid duplicates).
queue=()
seen_str=":"

queue_init() {
    local target="$1"
    while IFS= read -r line; do
        [ -n "$line" ] && queue+=("$line")
    done < <(collect "$target")
}

mark_and_test() {
    local needle="$1"
    case "$seen_str" in
        *":$needle:"*) return 1 ;;  # already seen
    esac
    seen_str="${seen_str}${needle}:"
    return 0
}

queue_init "$APP/Contents/MacOS/miditoqwerty"

while [ ${#queue[@]} -gt 0 ]; do
    dep="${queue[0]}"
    # Pop element 0; works on bash 3.2.
    queue=("${queue[@]:1}")
    [ -z "$dep" ] && continue
    mark_and_test "$dep" || continue

    base=$(basename "$dep")
    if [ ! -f "$dep" ]; then
        echo "warn: missing $dep, skipping" >&2
        continue
    fi

    cp "$dep" "$APP/Contents/Frameworks/$base"
    chmod u+w "$APP/Contents/Frameworks/$base"
    install_name_tool -id "@executable_path/../Frameworks/$base" \
        "$APP/Contents/Frameworks/$base"

    install_name_tool -change "$dep" "@executable_path/../Frameworks/$base" \
        "$APP/Contents/MacOS/miditoqwerty" 2>/dev/null || true

    while IFS= read -r sub; do
        [ -n "$sub" ] && queue+=("$sub")
    done < <(collect "$APP/Contents/Frameworks/$base")
done

# Second pass: rewrite cross-references between bundled dylibs.
for lib in "$APP/Contents/Frameworks/"*.dylib; do
    [ -f "$lib" ] || continue
    while IFS= read -r dep; do
        base=$(basename "$dep")
        if [ -f "$APP/Contents/Frameworks/$base" ]; then
            install_name_tool -change "$dep" "@executable_path/../Frameworks/$base" "$lib" 2>/dev/null || true
        fi
    done < <(collect "$lib")
done

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key><string>miditoqwerty</string>
    <key>CFBundleIdentifier</key><string>com.tannertunstall.miditoqwerty</string>
    <key>CFBundleName</key><string>MIDI to Qwerty</string>
    <key>CFBundleDisplayName</key><string>MIDI to Qwerty</string>
    <key>CFBundleVersion</key><string>${VERSION}</string>
    <key>CFBundleShortVersionString</key><string>${VERSION}</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>LSMinimumSystemVersion</key><string>10.15</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST


# Ad-hoc re-sign every dylib + the main binary + the bundle. install_name_tool
# above invalidates whatever signature clang applied during the build, so
# without this Apple Silicon's "must be signed" rule kicks in and the app
# is reported as damaged when a downloaded copy is launched. Ad-hoc (--sign -)
# is enough to pass that check; real Apple Developer ID notarization is a
# separate concern and is not done here.
for lib in "$APP/Contents/Frameworks/"*.dylib; do
    [ -f "$lib" ] || continue
    codesign --sign - --force --timestamp=none "$lib" >/dev/null 2>&1 || true
done
codesign --sign - --force --timestamp=none "$APP/Contents/MacOS/miditoqwerty" >/dev/null 2>&1 || true
codesign --sign - --force --deep --timestamp=none "$APP" >/dev/null 2>&1 || true

echo "Built $APP"
echo "  frameworks: $(ls "$APP/Contents/Frameworks" 2>/dev/null | tr '\n' ' ')"
echo "  signature : $(codesign -dv "$APP" 2>&1 | grep -E 'Signature=' || echo 'ad-hoc')"
