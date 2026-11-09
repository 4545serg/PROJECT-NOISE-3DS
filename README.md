# PROJECT SLITHER

A native MP3 player for the Nintendo 3DS family, written in C against libctru + citro2d.
Targets **Old 3DS / 3DS XL / 2DS** (no New 3DS features required; New 3DS simply runs it faster).

> **Build status:** the source was written and checked on a machine without the 3DS
> toolchain, so no `.3dsx` / `.cia` binary has been produced or run on hardware yet.
> Section 3 gets you both files without installing anything locally. Expect to report
> back build errors or hardware quirks - the code has had host-side syntax/type checks
> and parser tests only.

## 1. What it does

**Top screen:** `NOW PLAYING` heading, centred album art (animated snake placeholder
when a file has none), title, artist, progress bar, current time / total time.

**Bottom screen:** folder + MP3 browser (starts in `SD:/Music/`), the playing song is
highlighted, touch controls: previous / play-pause / next / shuffle / repeat / menu.

| Control | Action |
|---|---|
| **L / R** | Previous / next track |
| **A** | Play the selected song (opens the selected folder, or `..` to go up; resumes if paused) |
| **B** | Pause |
| **D-pad Up/Down** | Move through the list (hold to scroll fast) |
| **D-pad Left/Right** | Page up/down in the list; in settings: adjust the value |
| **SELECT** | Open / close settings |
| **START** | Exit |
| **X / Y** | Shuffle on-off / cycle repeat (off -> all -> one) - extras |
| **Touch** | Tap a row to open/play, drag to scroll, tap the on-screen buttons |

Music keeps playing while you browse folders or change settings. When a track ends the
next one starts (respecting shuffle/repeat). Previous restarts the track if more than
3 s have played.

**Settings (SELECT):** Background, Text, Accent and Highlight colours (per-channel R/G/B,
A switches channel, Left/Right or the touch +/- change it), five theme presets, volume.
Everything saves automatically to `SD:/3ds/PROJECT_SLITHER/settings.cfg`, along with
shuffle, repeat and the last folder, and is restored on next launch.

## 2. Putting music on the SD card

1. Power off the console and put its SD card in your PC (FAT32, as formatted by the 3DS).
2. Create (or let the app create on first launch) the folder `Music` at the top level:
   `SD:/Music/`.
3. Copy `.mp3` files there. Sub-folders are fine, e.g. `SD:/Music/Artist/Album/01 Song.mp3`.
4. Put the card back and launch the app. It lists only folders and `.mp3` files.

Tips
- Titles/artists come from ID3 tags (ID3v2.2/2.3/2.4, falling back to ID3v1, then the
  file name). Fix wrong tags with a tag editor such as Mp3tag or Kid3.
- Album art is read from the embedded ID3v2 picture (JPEG or PNG). To keep memory use low
  on Old 3DS, pictures above ~1.2 megapixels (about 1100x1100) are skipped and the
  placeholder is shown - re-embed a 500-600 px cover for best results. Baseline (non-progressive)
  JPEGs load fastest. Loading a cover can pause the UI for a moment (audio keeps playing).
- Duration uses the Xing/VBRI header when present, otherwise a bitrate estimate, so it can
  be slightly off for some VBR files without a header.
- **Sound requires the DSP firmware** at `SD:/3ds/dspfirm.cdc`. If the top screen says
  "dspfirm.cdc missing", run the *DSP1* homebrew once to dump it.

## 3. Building

### A. Easiest - GitHub builds it for you (nothing to install)
1. Create a GitHub repository and upload this whole folder (including `.github/`).
2. Open the **Actions** tab -> *Build PROJECT SLITHER* -> it runs on every push
   (or press *Run workflow*).
3. Download the artifacts: `project_slither-3dsx` and `project_slither-cia`.
   If the CIA artifact is missing, the makerom/bannertool step failed (it is
   `continue-on-error`); the `.3dsx` is still published - see B/C to make the CIA.

### B. Locally with devkitPro
1. Install devkitPro (https://devkitpro.org/wiki/Getting_Started) and the 3DS packages:
   `dkp-pacman -S 3ds-dev`
2. In a devkitPro shell (MSYS2 on Windows) in this folder:
   ```
   sh tools/get_deps.sh     # downloads minimp3.h and stb_image.h (or: make deps)
   make                     # -> project_slither.3dsx
   ```
   If you cannot run the script, download `minimp3.h` (lieff/minimp3) and `stb_image.h`
   (nothings/stb) into `source/` by hand.

### C. The CIA locally
`make cia` needs **makerom** and **bannertool** on your PATH (prebuilt Windows/Linux
builds are widely available; sources: 3DSGuy/Project_CTR and Steveice10/bannertool). Then:
```
make cia                 # -> project_slither.cia
```
`resources/app.rsf` holds the title ID (`0x7C531`), 64 MB application memory (Old 3DS
compatible) and the `dsp::DSP` service the audio needs. Change `UniqueId` if it clashes
with another title you have installed.

## 4. Installing

- **3DSX (testing):** copy `project_slither.3dsx` (and the `.smdh`) to `SD:/3ds/` and launch
  from the Homebrew Launcher.
- **CIA:** copy `project_slither.cia` anywhere on the SD card (e.g. `SD:/cias/`), open
  **FBI** -> *SD* -> select the file -> *Install CIA* (then *Delete* the file if you like).
  The app appears on the HOME Menu as PROJECT SLITHER.

## 5. Troubleshooting

| Symptom | Fix |
|---|---|
| No sound, red status line says DSP firmware missing | Dump `dspfirm.cdc` with DSP1 |
| Empty list | Files must be `.mp3` inside `SD:/Music/` |
| Stutter on very high bitrate files | Decoding runs on the main CPU core at 268 MHz; 320 kbps CBR/VBR is the practical upper end - re-encode at 192-256 kbps if needed |
| Placeholder instead of cover | Cover is not JPEG/PNG, is too large, or is not embedded as an ID3v2 picture |
| Settings not remembered | SD card is write-protected or full |

## 6. Source layout

```
source/main.c      UI, input, playlist logic, settings page
source/audio.c     minimp3 decode thread -> ndsp channel 0, position tracking
source/meta.c      ID3v2/v1 parsing, artwork extraction, duration probe
source/art.c       JPEG/PNG -> 128x128 tiled GPU texture
source/browser.c   folder listing / sorting
source/settings.c  theme + preferences, autosave file
resources/         app.rsf, icon, banner (generated by tools/make_assets.py)
tools/             get_deps.sh, make_assets.py
```

Licenses and attribution: see `LICENSE` and `THIRD_PARTY_NOTICES.md`.
