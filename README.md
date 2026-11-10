# PROJECT NOISE

A native MP3 / FLAC / WAV player for the Nintendo 3DS family, written in C against libctru + citro2d.
Targets **Old 3DS / 3DS XL / 2DS** (no New 3DS features required; New 3DS simply runs it faster).

> **Build status:** the source was written and checked on a machine without the 3DS
> toolchain, so no `.3dsx` / `.cia` binary has been produced or run on hardware yet.
> Section 3 gets you both files without installing anything locally. Expect to report
> back build errors or hardware quirks - the code has had host-side syntax/type checks
> and parser tests only.

## 1. What it does

**Top screen:** a *NOW PLAYING* header, then a Cover Flow of the album art (3 covers each side) around the highlighted song in the library
(covers load on a background thread, so scrolling stays smooth, and are cached on the SD card
in `SD:/3ds/PROJECT_NOISE/covers/` so they appear instantly next time - delete the folder to clear it). Picking a song folds the side covers away
and leaves the now-playing view (art, title, artist, progress bar, times); scroll the list
again and the covers fan back out. Songs without art show the animated logo cover.

The background around the cover carries an **ambient glow** taken from the artwork's dominant colour: it follows
the cover in the middle of the screen (the focused song while you browse, the playing song once you pick one) and
fades smoothly between covers. Folders and covers without art glow in your accent colour. Turn it off or choose
*Soft* / *Vivid* in Settings > Appearance > *Ambient glow*.

**Bottom screen:** folder + music browser (starts in `SD:/Music/`) with a **search bar** in the header, the playing
song is highlighted, touch controls: shuffle / previous / play-pause / next / repeat / menu (shuffle and repeat sit on
either side of the transport buttons and light up, with a dot underneath, when on; repeat-one shows a small "1").

| Control | Action |
|---|---|
| **L / R** | Previous / next song (previous restarts the song if more than 3 s have played); the list highlight follows the playing song |
| **D-pad Left/Right** | Skip back / forward 5 seconds (hold to keep skipping; past the end of a song = next track). The length is adjustable in Settings > General > *Skip length*. In settings: adjust the value |
| **A** | Play the selected song (opens the selected folder, or `..` to go up; resumes if paused) |
| **B** | Pause |
| **D-pad Up/Down** | Move through the list (hold to scroll fast) |
| **SELECT** | Open / close settings |
| **START** | Exit (asks "Exit PROJECT NOISE?" first; press START again or A on *Exit* to leave) |
| **X** | Search (opens the on-screen keyboard) |
| **Y** | Shuffle on / off (repeat: tap the repeat button on the touch screen) |
| **ZR** | Add the selected song to the play queue; queued songs play before the folder playlist advances |
| **ZL** | Favorite / unfavorite the selected song |
| **Touch** | Tap a row to open/play, drag to scroll, tap the search bar or the on-screen buttons |

Music keeps playing while you browse folders or change settings. When a track ends the
next one starts (respecting shuffle/repeat). The on-screen previous / next buttons change track; previous restarts the track if more than
3 s have played. Press **ZR** on a highlighted song to add it to the manual queue.

### Searching

Tap the **Search songs** bar (or press **X**) and type with the 3DS keyboard. The search covers **every song under
`SD:/Music/`, including all sub-folders**, and matches against file and folder names, ignoring case; every word you type
has to match, so `daft punk` finds `Daft Punk/Discovery/01 One More Time.mp3` and `radio ok` finds the OK Computer tracks.
Songs whose own file name matches are listed first. The result list works like any folder: tap or press A to play, the
cover flow follows your selection, and next / previous / shuffle / repeat move through the results. Tap the **x** in the
search bar (or the **Back** row) to return to the folder you searched from. The first search of a session reads the Music
folder once ("Searching..." for a moment on big libraries); after that, searches are instant.
It searches names, not ID3 tags - name your files `Artist - Title.mp3` (or keep them in Artist/Album folders) for best results.

**Settings (SELECT or the menu button):** three tabs.
- **Appearance** - a theme preset (Noise, Emerald, Ocean, Sunset, Mono), an accent colour swatch and the **Ambient glow**
  (Off / Soft / Vivid).
- **Audio** - a **Manual EQ** switch and a **5-band equalizer** shown as five **vertical sliders** (60 Hz, 230 Hz,
  910 Hz, 3.6 kHz, 14 kHz, -12..+12 dB, with the gain above each slider) and presets (Flat, Bass Boost, Treble, Vocal, Rock, Pop).
  The 3DS hardware volume slider controls listening volume. Manual EQ **off**: the chosen preset plays and the sliders show its curve (greyed, locked; touching one reminds you to turn Manual EQ on).
  Manual EQ **on**: your own slider settings play (the preset selector reads "User"). Your manual curve is
  remembered while you switch back and forth. Drag a slider up / down with the stylus or a finger; with the D-pad,
  Left/Right picks the band, Up/Down changes its gain, and A (or Left / Right past the first / last band) leaves the sliders.
- **General** - **Skip length** (what D-pad Left/Right skip: 5 / 10 / 15 / 30 seconds), **Clock** (off / 12-hour / 24-hour)
  and **Battery percentage**, shown in the top-screen corners; **Show setup guide** (replays the first-run guide) and
  **Exit app** (asks for confirmation).

Everything saves automatically to `SD:/3ds/PROJECT_NOISE/settings.cfg`, along with
shuffle, repeat, EQ and the last folder, and is restored on next launch.

**First launch:** a three-step setup runs once - (1) welcome + a check that sound and the music folder are ready,
(2) **Make it yours**: theme, accent, ambient glow, clock and battery on a single page, with the top screen showing
the result live, (3) the controls cheat-sheet. Use the touch buttons or A = Next, B = Back; *Skip* leaves it early.
The equalizer and skip length live in Settings. Replay the setup any time from Settings > General > *Show setup guide*.

## 2. Putting music on the SD card

1. Power off the console and put its SD card in your PC (FAT32, as formatted by the 3DS).
2. Create (or let the app create on first launch) the folder `Music` at the top level:
   `SD:/Music/`.
3. Copy `.mp3`, `.flac` or `.wav` files there. Sub-folders are fine, e.g. `SD:/Music/Artist/Album/01 Song.mp3`.
4. Put the card back and launch the app. It lists only folders and `.mp3` / `.flac` / `.wav` files.

Tips
- Formats: MP3, FLAC (up to 8 channels, mixed down to stereo) and uncompressed WAV (8/16/24/32-bit PCM or 32-bit float).
  FLAC reads Vorbis comments + embedded pictures; WAV reads LIST/INFO tags. Hi-res FLAC (96 kHz+) may stutter on Old 3DS.
- Titles/artists come from ID3 tags (ID3v2.2/2.3/2.4, falling back to ID3v1, then the
  file name). Fix wrong tags with a tag editor such as Mp3tag or Kid3.
- Album art is read from the embedded ID3v2 picture (JPEG or PNG). To keep memory use low
  on Old 3DS, pictures above ~1.2 megapixels (about 1100x1100) are skipped and the
  placeholder is shown - re-embed a 500-600 px cover for best results. Baseline (non-progressive)
  JPEGs load fastest. Loading a cover can pause the UI for a moment (audio keeps playing).
- Skipping with D-pad Left / Right is exact for FLAC and WAV; for MP3 it is estimated from file position, so VBR files can land
  a little off.
- Duration uses the Xing/VBRI header when present, otherwise a bitrate estimate, so it can
  be slightly off for some VBR files without a header.
- **Sound requires the DSP firmware** at `SD:/3ds/dspfirm.cdc`. If the top screen says
  "dspfirm.cdc missing", run the *DSP1* homebrew once to dump it.

## 3. Building

### A. Easiest - GitHub builds it for you (nothing to install)
1. Create a GitHub repository and upload this whole folder (including `.github/`).
2. Open the **Actions** tab -> *Build PROJECT NOISE* -> it runs on every push
   (or press *Run workflow*).
3. Download the artifacts: `PROJECT-NOISE-3DSX` (the `.3dsx` + `.smdh`). To make a CIA, see C.

### B. Locally with devkitPro
1. Install devkitPro (https://devkitpro.org/wiki/Getting_Started) and the 3DS packages:
   `dkp-pacman -S 3ds-dev`
2. In a devkitPro shell (MSYS2 on Windows) in this folder:
   ```
   sh tools/get_deps.sh     # downloads minimp3.h, stb_image.h and dr_flac.h (or: make deps)
   make                     # -> project_noise.3dsx
   ```
   If you cannot run the script, download `minimp3.h` (lieff/minimp3), `stb_image.h`
   (nothings/stb) and `dr_flac.h` (mackron/dr_libs) into `source/` by hand.

### C. The CIA locally
`make cia` needs **makerom** and **bannertool** on your PATH (prebuilt Windows/Linux
builds are widely available; sources: 3DSGuy/Project_CTR and Steveice10/bannertool). Then:
```
make cia                 # -> project_noise.cia
```
`resources/app.rsf` holds the title ID (`0x7C531`), 64 MB application memory (Old 3DS
compatible) and the `dsp::DSP` service the audio needs. Change `UniqueId` if it clashes
with another title you have installed.

## 4. Installing

- **3DSX (testing):** copy `project_noise.3dsx` (and the `.smdh`) to `SD:/3ds/` and launch
  from the Homebrew Launcher.
- **CIA:** copy `project_noise.cia` anywhere on the SD card (e.g. `SD:/cias/`), open
  **FBI** -> *SD* -> select the file -> *Install CIA* (then *Delete* the file if you like).
  The app appears on the HOME Menu as PROJECT NOISE.

## 5. Troubleshooting

| Symptom | Fix |
|---|---|
| No sound, red status line says DSP firmware missing | Dump `dspfirm.cdc` with DSP1 |
| Empty list | Files must be `.mp3`, `.flac` or `.wav` inside `SD:/Music/` |
| Search finds nothing | It matches file / folder names under `SD:/Music/` (not ID3 tags), and every typed word must match |
| Stutter on very high bitrate files | Decoding runs on the main CPU core at 268 MHz; 320 kbps CBR/VBR is the practical upper end - re-encode at 192-256 kbps if needed |
| Placeholder instead of cover | Cover is not JPEG/PNG, is too large, or is not embedded as an ID3v2 picture |
| Settings not remembered | SD card is write-protected or full |

## 6. Source layout

```
source/main.c      UI, input, playlist logic, ambient glow, settings page (incl. vertical EQ), search UI, first-run setup, clock/battery
source/search.c    song search: one-time index of SD:/Music/ (recursive) + in-memory filter
source/audio.c     minimp3 / dr_flac / WAV decode thread -> ndsp channel 0, position tracking
source/meta.c      ID3v2/v1 parsing, artwork extraction, duration probe
source/art.c       JPEG/PNG -> 128x128 tiled GPU texture (decode + upload split) + dominant-colour extraction for the glow
source/flow.c      Cover Flow: perspective drawing, background cover loader, SD thumbnail cache
source/browser.c   folder listing / sorting
source/settings.c  theme + preferences, autosave file
resources/         app.rsf, icon, banner (generated by tools/make_assets.py)
tools/             get_deps.sh, make_assets.py
```

Licenses and attribution: see `LICENSE` and `THIRD_PARTY_NOTICES.md`.
