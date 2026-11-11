#!/bin/sh
# Downloads the three single-header libraries PROJECT NOISE vendors at build time.
#   minimp3.h   (CC0)                   https://github.com/lieff/minimp3
#   stb_image.h (public domain / MIT)   https://github.com/nothings/stb
#   dr_flac.h   (public domain / MIT-0) https://github.com/mackron/dr_libs
#
# By default the latest `master` of each library is used. For reproducible builds pin a
# commit or tag per library, e.g.:
#   MINIMP3_REF=<commit> STB_REF=<commit> DRLIBS_REF=<commit> sh tools/get_deps.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p source

MINIMP3_REF="${MINIMP3_REF:-master}"
STB_REF="${STB_REF:-master}"
DRLIBS_REF="${DRLIBS_REF:-master}"

# fetch <url> <dest> <marker>
#   Downloads to a temporary file first and only moves it into place once it looks like the
#   right header (contains <marker>), so an interrupted download, an HTML error page or a
#   parallel `make -j` can never leave a broken header behind.
fetch() {
    url="$1"; dest="$2"; marker="$3"
    if [ -s "$dest" ]; then echo "have $dest"; return; fi
    echo "downloading $dest"
    tmp="$dest.part.$$"
    trap 'rm -f "$tmp"' EXIT INT TERM
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --retry 3 "$url" -o "$tmp"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -t 3 "$url" -O "$tmp"
    else
        echo "Need curl or wget, or download $url to $dest by hand." >&2
        exit 1
    fi
    if [ ! -s "$tmp" ] || ! grep -q "$marker" "$tmp"; then
        echo "Download of $dest failed (unexpected content from $url)" >&2
        exit 1
    fi
    mv -f "$tmp" "$dest"
    trap - EXIT INT TERM
}

fetch "https://raw.githubusercontent.com/lieff/minimp3/$MINIMP3_REF/minimp3.h"       source/minimp3.h   mp3dec_decode_frame
fetch "https://raw.githubusercontent.com/nothings/stb/$STB_REF/stb_image.h"          source/stb_image.h stbi_load_from_memory
fetch "https://raw.githubusercontent.com/mackron/dr_libs/$DRLIBS_REF/dr_flac.h"      source/dr_flac.h   drflac_open_file
echo "dependencies ready"
