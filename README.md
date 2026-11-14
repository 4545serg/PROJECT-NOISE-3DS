# PROJECT NOISE

A native **MP3 / FLAC / WAV** player for the Nintendo 3DS family, written in C against libctru + citro2d.
Targets **Old 3DS / 3DS XL / 2DS** (no New 3DS features required; New 3DS simply runs it faster).

> **Build status:** the source was written and checked on a machine without the 3DS toolchain, so no
> `.3dsx` / `.cia` has been produced or run on hardware yet. The code has had host-side syntax and
> parser checks only. Section 3 builds both files without installing anything locally - expect to
> report back build errors or hardware quirks.

## Contents

1. [What it does](#1-what-it-does)
2. [Putting music on the SD card](#2-putting-music-on-the-sd-card)
3. [Building](#3-building)
4. [Installing](#4-installing)
5. [Troubleshooting](#5-troubleshooting)
6. [Source layout](#6-source-layout)

## 1. What it does

**Top screen** - a *NOW PLAYING* header and a **Cover Flow** of album art (3 covers each side) around the
highlighted song. Covers load on a background thread, so scrolling stays smooth, and are cached on the SD
card in `SD:/3ds/PROJECT_NOISE/covers/` so they appear instantly next time (delete the folder to clear it).
Picking a song folds the side covers away and leaves the now-playing view (art, title, artist, progress bar,
times); scroll the list again and the covers fan back out. Songs without art show the default logo cover.

A minimal boot screen shows the app mark, a small equaliser animation, a thin progress bar and **MADE BY GRIMZZZZ** on launch (a quiet task checklist runs on the bottom screen). The background around the
cover carries an **ambient glow** taken from the artwork's dominant colour: it follows the cover in the
middle of the screen and fades smoothly between covers. Folders and covers without art glow in your accent
colour. Turn it off, or pick *Soft* / *Vivid*, in Settings > Display > *Ambient glow*.

**Bottom screen** - folder + music browser (starts in `SD:/Music/`) with a **search bar** and a
**Favorites** heart in the header. The playing song is highlighted. Touch controls sit along the bottom:
shuffle / previous / play-pause / next / repeat / Up Next. Shuffle and repeat light up with a dot underneath
when on; repeat-one shows a small "1".

### Controls

| Control | Action |
|---|---|
| **L / R** | Previous / next song (previous restarts the song if more than 3 s have played); the list highlight follows the playing song |
| **D-pad Left / Right** | Skip back / forward (default 5 s, hold to keep skipping; past the end of a song = next track). Length is set in Settings > Playback > *Skip length*. In Settings: adjust the selected value |
| **D-pad Up / Down** | Move through the list (hold to scroll fast) |
| **A** | Play the selected song; open the selected folder (or `..` to go up); resume if paused. In Settings: step the selected value |
| **B** | Pause. Closes Settings / Up Next |
| **X** | Add the selected song to the **front** of Up Next (plays next) |
| **Y** | Add the selected song to the **end** of Up Next |
| **ZL** | Favorite / unfavorite the selected song |
| **SELECT** | Open / close Settings |
| **START** | Exit - asks "Exit PROJECT NOISE?" first (press START again, or A on *Exit*, to leave) |
| **Touch** | Tap a row to open/play, drag to scroll, tap the search bar, the header heart, the transport controls or the Up Next button; drag the seek bar to jump around in a song |

Music keeps playing while you browse folders or change settings. When a track ends the next one starts
(respecting shuffle / repeat). The 3DS hardware volume slider controls loudness.

### Up Next

Press **Y** on a song to queue it, or **X** to make it play next. Tap the queue icon at the right of the
mini-player to open the **Up Next** screen: tap an item (or press A) to play it immediately, tap the **×**
on a row to remove it, tap **Done** (or press B) to go back. The mini-player and its transport controls stay
available there, and X / Y do nothing destructive on that screen. Re-adding a queued song moves it instead
of duplicating it.

### Favorites

Tap the **♥** at the right of a song row, or press **ZL** on the selected song, to favorite or unfavorite it. Tap
the heart in the header (next to the title) to switch to the Favorites list, which plays and shuffles like any
other playlist. Favorites are saved to
`SD:/3ds/PROJECT_NOISE/favorites.txt`.

### Searching

Tap the **Search songs** bar and type with the 3DS keyboard. The search covers **every song under
`SD:/Music/`, including all sub-folders**, and matches file and folder names, ignoring case. Every word you
type has to match, so `daft punk` finds `Daft Punk/Discovery/01 One More Time.mp3` and `radio ok` finds the
OK Computer tracks. Songs whose own file name matches are listed first.

The result list works like any folder: tap or press A to play, the cover flow follows your selection, and
next / previous / shuffle / repeat move through the results. Tap the **x** in the search bar (or the **Back**
row) to return to the folder you searched from. The first search of a session reads the Music folder once
("Searching..." for a moment on big libraries); after that, searches are instant.
It searches *names*, not ID3 tags - name your files `Artist - Title.mp3`, or keep them in Artist/Album
folders, for the best results.

### Settings (SELECT)

Seven sections. Tap the left / right half of the switcher bar under the title (or press D-pad Down / Up past the
first / last row) to change section; the dots beside the title show where you are. A section with more rows than
fit (Display) scrolls: the highlight scrolls it with the D-pad, and the small arrows at the right edge scroll it by touch.

- **Playback** - **Remember song** (reopens on the last song and position), **Skip length** (5 / 10 / 15 / 30 s) and
  **Track info** (elapsed, length, format and bitrate).
- **Audio** - a **Manual EQ** switch and a **5-band equalizer** drawn as five vertical sliders (60 Hz,
  230 Hz, 910 Hz, 3.6 kHz, 14 kHz; -12..+12 dB, gain shown above each slider), plus presets (Flat, Bass Boost,
  Treble, Vocal, Rock, Pop).
  - Manual EQ **off**: the chosen preset plays and the sliders show its curve (greyed and locked; touching one
    reminds you to turn Manual EQ on).
  - Manual EQ **on**: your own slider settings play and the preset selector reads "User". Your manual curve is
    remembered while you switch back and forth.
  - Drag a slider with the stylus or a finger; with the D-pad, Left/Right picks the band, Up/Down changes its
    gain, and A (or Left / Right past the first / last band) leaves the sliders.
- **Display** - **OLED mode** (true-black background with neon accents and glowing text), **Glow color** (Auto, Cyan, Mint,
  Amber, Magenta, Violet, Crimson, Ice - tints the ambient glow, the halos around panels and the NOW PLAYING glow),
  **Ambient glow** (Off / Soft / Vivid), **Album colors** (the accent follows the playing cover; switch it off to keep your
  theme colour), **Visualizer** (Cover / LED Spectrum / Scope / VU Deck), **Cover fade**, **Keyboard font**, **Clock**
  (off / 12-hour / 24-hour), **Battery** (on / off) and **Battery style** (Icon / Percent). The theme preset and
  wallpaper are chosen in the setup guide (see **Replay guide** under About).
- **Background Playback** - **Play lid closed** (keeps the music going when you shut the lid; see below) and
  **Battery saver**.
- **Timers** - a **Sleep timer** switch with a 5 / 10 / 15 / 30 / 45 / 60-minute **Timer length** (the countdown
  shows on the top screen; changing the duration restarts a running timer).
- **Library** - lyrics: **Automatic search**, **Auto download**, **Offline only**, **Prefer synced**, **Animated lyrics**,
  **Animation style**, **Lyrics font** and **Clear lyrics cache** (see Lyrics below).
- **About** - **Replay guide**, **Exit app** (asks for confirmation) and the PROJECT NOISE / **MADE BY GRIMZZZZ** credit.

**Battery saver** draws at 30 fps, simplifies the visualizers, dims the glow, turns off the breathing / pop
animations and screen fades, and polls the battery less often.

**Top screen**: one header bar (PROJECT NOISE, clock, battery), the cover, then **NOW PLAYING** / **BROWSING**
directly under it (the NOW PLAYING label glows while music plays). On the Now Playing view two small cards beside
the cover show elapsed / length and format / bitrate; the visualizer displays carry the same read-out in their
title strip. Audio format is taken from the file type and the bitrate is the file's average bitrate.

Everything saves automatically to `SD:/3ds/PROJECT_NOISE/settings.cfg` - theme and options, shuffle, repeat,
EQ and the last folder - and is restored on the next launch. The last song and its position are kept in
`SD:/3ds/PROJECT_NOISE/resume.cfg`; on the next launch it is shown paused at that spot and Play continues from there. Closing the lid pauses playback; opening it
resumes.

**Lyrics** - tap **LYRICS** (next to the search bar) while a song is loaded. Lyrics are found in this order:
`<song>.lrc` next to the file, the SD cache `SD:/3ds/PROJECT_NOISE/lyrics/`, then online: LRCLIB (exact lookup, then search), LRCMux (multi-provider fallback), an optional
external service, and lyrics.ovh. Artist / title come from the file's tags (ID3, Vorbis comments, WAV INFO), or
from an `Artist - Title` file name. Remixes, covers, live and karaoke versions are rejected unless the song itself is
one, and the length must roughly match. Title / artist are cleaned before searching (`- Remastered`, `feat.`, brackets, accents,
`A; B` credits). Synchronized (LRC) lyrics highlight the current line and scroll with the song;
plain lyrics scroll with the D-pad. Searching runs on a background thread, so playback never waits for it.
Settings > Library: *Automatic search* (look online when the lyrics screen opens; off = press A), *Auto download*
(keep results in the SD cache; off = shown only), *Offline only* (cache and `.lrc` files only), *Prefer synced*,
*Clear lyrics cache*. Songs nobody has are remembered for 7 days so they are not searched again.

PROJECT NOISE uses the devkitPro 3DS curl + mbedTLS stack for online lyrics (the native http:C path is only a fallback). The app initializes the 3DS SOC socket service before libcurl. Building requires the `3ds-curl` and `3ds-mbedtls` devkitPro portlibs.

Lyrics are public, read-only text, so they are fetched **without** a certificate check by default: parsing the whole CA bundle takes several seconds on an Old 3DS and a missing bundle used to make every search fail. Set `verify_tls=1` in `lyrics.cfg` to check certificates against `SD:/config/ssl/cacert.pem` (skipped, not failed, when the file is missing). A service `token` is only ever sent over a verified connection. Replies are requested gzip-compressed and an oversized reply is cut instead of failing.

A search never stays on "searching": it has a shared 12 s budget and the screen gives up after 16 s even if the network stack is stuck. Whenever nothing could be found - or the servers could not be reached - the lyrics screen simply shows **LYRICS NOT FOUND** (press A to search again; only real "nobody has it" answers are remembered for 7 days). An optional service can be configured by creating
`SD:/3ds/PROJECT_NOISE/lyrics.cfg`:

```
proxy=https://your-service.example/lyrics     # plain http:// also works
token=optional-secret                           # sent as "Authorization: Bearer <token>"
verify_tls=0                                    # 1 = check certificates against SD:/config/ssl/cacert.pem (slower)
insecure_tls=0                                  # 1 = never check certificates, even for the service token (not recommended)
```

The service receives `GET <proxy>?artist_name=..&track_name=..&duration=<seconds>&synced=0|1` and answers with
LRCLIB-style JSON: one object or an array of `{trackName, artistName, duration, plainLyrics, syncedLyrics}`
(or just `{"lyrics": "..."}`). No key is built into the app.

**Loading screen** - a Windows-style window with a progress bar and the current task. It loads the audio engine,
UI assets, the music folder, the last song, the selected song's tags + cover and its *cached* lyrics (never online);
all other covers keep loading in the background.

### First launch

A three-step setup runs once: (1) welcome plus a check that sound and the music folder are ready, (2) **Make it
yours** - theme, ambient glow, clock and battery on one page, with the top screen showing the result live, (3)
a controls cheat-sheet. Use the touch buttons or **A** = Next, **B** = Back; *Skip* leaves early. Replay it any
time from Settings > About > *Replay guide*.

### Limits

| What | Limit |
|---|---|
| Entries listed per folder / search result | 2,000 |
| Songs remembered by the search index | 30,000 (about 12 folder levels deep) |
| Up Next queue | 64 songs |
| Favorites | 512 songs |

## 2. Putting music on the SD card

1. Power off the console and put its SD card in your PC (FAT32, as formatted by the 3DS).
2. Create (or let the app create on first launch) the folder `Music` at the top level: `SD:/Music/`.
3. Copy `.mp3`, `.flac` or `.wav` files there. Sub-folders are fine, e.g. `SD:/Music/Artist/Album/01 Song.mp3`.
4. Put the card back and launch the app. It lists only folders and `.mp3` / `.flac` / `.wav` files.

**Tips**

- **Formats:** MP3, FLAC (up to 8 channels, mixed down to stereo) and uncompressed WAV (8/16/24/32-bit PCM or
  32-bit float). FLAC reads Vorbis comments and embedded pictures; WAV reads LIST/INFO tags. Hi-res FLAC
  (96 kHz and up) may stutter on Old 3DS.
- **Tags:** titles and artists come from ID3 tags (ID3v2.2 / 2.3 / 2.4, falling back to ID3v1, then the file
  name). Fix wrong tags with a tag editor such as Mp3tag or Kid3.
- **Album art** is read from the embedded ID3v2 picture (JPEG or PNG). To keep memory use low on Old 3DS,
  pictures above ~1.2 megapixels (about 1100x1100) are skipped and the placeholder is shown - re-embed a
  500-600 px cover for best results. Baseline (non-progressive) JPEGs load fastest. Loading a cover can pause
  the UI for a moment (audio keeps playing).
- **Skipping** with D-pad Left / Right is exact for FLAC and WAV; for MP3 it is estimated from the file
  position, so VBR files can land a little off.
- **Duration** uses the Xing/VBRI header when present, otherwise a bitrate estimate, so it can be slightly off
  for some VBR files without a header.
- **Sound requires the DSP firmware** at `SD:/3ds/dspfirm.cdc`. If the top screen says "dspfirm.cdc missing",
  run the *DSP1* homebrew once to dump it.

## 3. Building

### A. Easiest - GitHub builds it for you (nothing to install)

1. Create a GitHub repository and upload this whole folder (including `.github/`).
2. Open the **Actions** tab -> *Build PROJECT NOISE*. It runs on every push (or press *Run workflow*).
3. Download the `PROJECT-NOISE-3DSX` artifact (the `.3dsx` + `.smdh`) and, if the best-effort CIA step
   succeeded, `PROJECT-NOISE-CIA` (the `.cia`). Otherwise see C.

### B. Locally with devkitPro

1. Install devkitPro (https://devkitpro.org/wiki/Getting_Started) and the 3DS packages:
   `dkp-pacman -S 3ds-dev`
2. In a devkitPro shell (MSYS2 on Windows), in this folder:
   ```
   make deps     # downloads minimp3.h, stb_image.h and dr_flac.h (make also does this on demand)
   make          # -> project_noise.3dsx
   ```
   If you cannot run the script, download `minimp3.h` (lieff/minimp3), `stb_image.h` (nothings/stb) and
   `dr_flac.h` (mackron/dr_libs) into `source/` by hand.

   The decoder headers are fetched from each project's `master` branch by default. For a reproducible build,
   pin them: `MINIMP3_REF=<commit> STB_REF=<commit> DRLIBS_REF=<commit> make deps`.

### C. The CIA locally

`make cia` needs **makerom** and **bannertool** on your PATH (prebuilt Windows/Linux builds are widely
available; sources: 3DSGuy/Project_CTR and Steveice10/bannertool):
```
make cia      # -> project_noise.cia
```
`resources/app.rsf` holds the title ID (`0x7C531`), 64 MB application memory (Old 3DS compatible) and the
`dsp::DSP` service the audio needs. Change `UniqueId` if it clashes with another title you have installed.
Note that makerom limits `BasicInfo/Title` to 8 characters.

## 4. Installing

- **3DSX (testing):** copy `project_noise.3dsx` (and the `.smdh`) to `SD:/3ds/` and launch it from the
  Homebrew Launcher.
- **CIA:** copy `project_noise.cia` anywhere on the SD card (e.g. `SD:/cias/`), open **FBI** -> *SD* -> select
  the file -> *Install CIA* (then *Delete* the file if you like). The app appears on the HOME Menu as
  PROJECT NOISE.

## 5. Troubleshooting

| Symptom | Fix |
|---|---|
| No sound, red status line says DSP firmware missing | Dump `dspfirm.cdc` with DSP1 |
| Empty list | Files must be `.mp3`, `.flac` or `.wav` inside `SD:/Music/` |
| Search finds nothing | It matches file / folder names under `SD:/Music/` (not ID3 tags), and every typed word must match |
| Stutter on very high bitrate files | Decoding runs on the main CPU core at 268 MHz; 320 kbps CBR/VBR is the practical upper end - re-encode at 192-256 kbps if needed |
| Placeholder instead of cover | Cover is not JPEG/PNG, is too large, or is not embedded as an ID3v2 picture |
| Settings or favorites not remembered | SD card is write-protected or full |
| Covers look stale after retagging | Delete `SD:/3ds/PROJECT_NOISE/covers/` to clear the cache |
| `make` fails downloading headers | Check your connection, or download the three headers into `source/` by hand (see 3B) |

## 6. Source layout

```
source/main.c        UI, input, playlist + Up Next, favorites, ambient glow, settings (incl. vertical EQ),
                     search UI, first-run setup, clock / battery, sleep timer
source/lyrics.c      lyrics: ID3/filename identification, LRC parser, SD cache, background HTTPS lookup + matching
source/search.c      song search: one-time index of SD:/Music/ (recursive) + in-memory filter
source/audio.c       minimp3 / dr_flac / WAV decode thread -> ndsp channel 0, 5-band EQ, position tracking
source/meta.c        ID3v2/v1, Vorbis comment and WAV tag parsing, artwork extraction, duration probe
source/art.c         JPEG/PNG -> 128x128 tiled GPU texture (decode + upload split) + dominant colour for the glow
source/flow.c        Cover Flow: perspective drawing, background cover loader, SD thumbnail cache
source/browser.c     folder listing / sorting
source/settings.c    theme + preferences, atomic autosave (settings.cfg)
source/noise.h       shared constants (app name, SD paths)
source/libcfg.h      configuration macros shared by every file that sees minimp3 / stb_image / dr_flac
source/*impl.c       the single translation units that hold the decoder library implementations
source/placeholder.h default cover art as a C array (generated by tools/make_cover.py)
resources/           app.rsf, icon, banner, banner jingle, default cover
tools/               get_deps.sh, make_assets.py, make_cover.py
```

Licenses and attribution: see `LICENSE` and `THIRD_PARTY_NOTICES.md`.

### Play with the lid closed

Settings > Background Playback > **Play lid closed** (off by default, saved as `lid_play`). While it is on and a song is playing,
the console is not allowed to sleep, so closing the lid keeps the music going (L / R still skip). The screens are
switched off while the lid is shut and come back when you open it. Pausing or stopping (or the sleep timer ending)
lets the console sleep as normal. With the switch off, closing the lid pauses and sleeps like before. Playing with
the lid closed uses more battery than system sleep, so turn it off when you do not need it.
