#!/bin/sh
# Downloads the two single-header libraries PROJECT SLITHER vendors at build time.
#   minimp3.h   (CC0)                   https://github.com/lieff/minimp3
#   stb_image.h (public domain / MIT)   https://github.com/nothings/stb
set -e
cd "$(dirname "$0")/.."
mkdir -p source

fetch() {
    url="$1"; dest="$2"
    if [ -s "$dest" ]; then echo "have $dest"; return; fi
    echo "downloading $dest"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url" -o "$dest"
    elif command -v wget >/dev/null 2>&1; then
        wget -q "$url" -O "$dest"
    else
        echo "Need curl or wget, or download $url to $dest by hand." >&2
        exit 1
    fi
    [ -s "$dest" ] || { echo "Download of $dest failed" >&2; rm -f "$dest"; exit 1; }
}

fetch https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h source/minimp3.h
fetch https://raw.githubusercontent.com/nothings/stb/master/stb_image.h source/stb_image.h
echo "dependencies ready"
