## 2026-11-11 — Lyrics network reliability

- Added a TLS fallback for the 3DS lyrics HTTP client: verified HTTPS is attempted first, then the same read-only request is retried with certificate verification disabled when the console cannot validate the provider certificate chain.
- Preserved background lookup, SD-card caching, LRCLIB lookup/fallbacks, and playback independence.

# Changelog

## Unreleased

### Added (lyrics + loading screen)
- **Lyrics**: LYRICS button, synced (LRC) and plain lyrics, SD cache, `.lrc` sidecar files, background online
  lookup (LRCLIB, optional external service via `lyrics.cfg`, lyrics.ovh) with strict artist / title / length matching.
- Settings > **Lyrics** tab: automatic search, auto download, offline only, prefer synced, clear cache.
- **Windows-style loading screen** with real progress: preloads tags and cover of the selected song, the last
  song and its cached lyrics; the rest loads in the background. Replaces the fixed 1.2 s splash.

### Added (UI update)
- **One seamless top bar**: PROJECT NOISE, clock and battery always share a single bar, whatever the clock setting.
- **Battery icon** with a level fill (amber / red when low, bolt while charging); Settings > General > *Battery
  style* switches between the icon and a percentage.
- **NOW PLAYING / BROWSING** label moved under the cover, larger, cross-fading, and glowing while music plays.
- **Glow color** presets (Settings > Look), **Album colors** (accent follows the cover) and **OLED black**.
- **Retro head-unit visualizers** (LED Spectrum, Scope, VU Deck): full width, bezel + dark glass, segmented
  LEDs with peak hold, scale markings, analogue needles, standby shimmer.
- **Music information**: elapsed, length, format and bitrate (cards beside the cover / visualizer title strip).
- **Playback memory**: last song and position restored on launch (paused; Play resumes from the spot).
- **Battery saver**: 30 fps drawing, lighter visualizers and glow, no breathing / pop / fade animations.
- Smooth animations: bottom-screen page fades, sliding tab highlight, gliding switches, button pop, toast slide,
  artwork settle + bass breathing.
- Settings now has four tabs (Look / Audio / Player / General); every new feature has its own control.

### Changed (UI, round 3)
- **Top screen lighting**: the ambient glow was not actually being drawn on the redesigned top screen. It is now
  rendered behind everything, tinted by the album art, and gently breathes with the bass. The screen outline is
  drawn as a thin ring so the glow shows through to the edges.
- Top screen: taller header, larger song title / artist (and times), clock shows the time only (no date).
- Bottom screen: three larger song rows (bigger title / artist / duration), a cleaner heart (even outline), and a
  Now Playing capsule with a bigger title and the artist underneath.
- A spinning **CD** replaces the equalizer icon next to the song title (top panel and bottom Now Playing); it
  spins while music plays and stops when paused.
- **Visualizer** (Settings > Appearance > Visualizer: Cover / Spectrum / Waveform / Orbit) now reacts to the audio
  actually playing: the audio thread analyses each decoded chunk (12 bands + waveform) and the UI reads the chunk
  that is playing right now.

### Fixed
- Status toasts ("Added to play next", "Already next", ...) no longer show list text through the pill (text is
  drawn at a higher depth than the pill while a toast is up).
- Previous track: pressing it after a few seconds now restarts the song that is actually playing (even one from
  Up Next); right after a queued song it returns to the song it interrupted.

### Changed (UI, round 2)
- Bottom screen: Home and the header "Liked Songs" button are gone; the tabs are now the top row and the
  search bar sits full-width underneath them. "Playlists" is now **Queue** (opens Up Next).
- New default theme **Noise**, using the logo blue (`#2F8CFF`); it also colours the boot screen. The old Neon
  green is still available as a theme. Existing settings files are migrated (old Neon -> Noise).
- Top screen: removed the ring around the cover. The ambient glow is now larger and stronger and takes its
  colour from the album art (Settings > Appearance > Ambient glow: Soft / Vivid).

### Fixed (repo layout)
- The neon UI, CIA workflow, fixed `app.rsf` and newer `get_deps.sh` were sitting loose in the repo root while the
  Makefile only builds `source/`, so builds kept producing the old UI. Everything now lives in `source/`,
  `resources/`, `tools/` and `.github/workflows/`; duplicate `file (N).ext` copies were removed.
- The CIA workflow had doubled backslashes (`\\`) in its `docker run` command, which breaks the shell line.
- Top-screen clock now reads `11/10 (Tue) 15:24`, as in the mock-up.

### Changed (UI)
- Rebuilt the bottom screen: cleaner header, **All Songs / Liked** tabs (Playlists removed), roomier song rows,
  a new now-playing deck, and the Neon green theme as the default. Fixed bottom-screen layering so panels no
  longer hide the rows drawn on top of them, and status toasts are drawn again.

### Added
- **Up Next screen**: the mini-player queue button opens a dedicated queue view with song titles, folder
  context, immediate play and per-item removal.
- **Manual play queue**: press Y on a selected song to add it to the end of Up Next, or X to prioritize it as the
  next track; queue-screen X/Y shortcuts are non-destructive.
- **Favorites** library section with per-song heart toggles (header heart, row heart or ZL); favorites persist on
  SD and can be shuffled as a playlist.
- **Sleep timer** (5 / 10 / 15 / 30 / 45 / 60 minutes) in Settings > General, with a live countdown, start/reset
  and cancel controls.
- **Cover fade** setting (Quick / Smooth / Slow, Settings > Appearance): the now-playing cover and glow fade in
  softly when a song starts.
- **Ambient glow**: the top-screen background takes on the dominant colour of the album art around the centre
  cover, easing smoothly between covers (Settings > Appearance > Ambient glow: Off / Soft / Vivid).
- **Skip length** setting (5 / 10 / 15 / 30 s) for the D-pad skip.
- Transport buttons flash briefly when pressed.
- `.gitignore` (build output and the downloaded decoder headers).
- `source/libcfg.h`: one shared configuration for the minimp3 / stb_image / dr_flac headers.

### Changed
- Refined the top-screen now-playing layout with more breathing room around track metadata and the progress bar;
  surfaced the queue count in the status area.
- Settings tabs are now **Appearance**, **Audio** and **General**; removed the D-pad/B help footer and the
  visible lid-playback option.
- Removed the in-app volume control; DSP output is kept at unity gain so the physical 3DS volume slider controls
  loudness.
- Softened the ambient lighting and top-screen background wash for more restrained contrast.
- **D-pad Left/Right** now skip back / forward; **L / R** now play the previous / next song.
- Skipping a song shows what is playing and moves the list highlight to it.
- First-run setup shortened from six steps to three; theme, glow, clock and battery share one page. Its controls
  page now lists ZL (Favorite).
- Simplified Appearance by removing the separate accent-colour picker; each theme now supplies its own
  coordinated colours.
- Improved Up Next guidance: larger, lower-positioned instructions clearly explain Y to queue and X to
  prioritize a song. The caption now says that tapping a song plays it *now* (it used to say "next").
- Cover-flow reflections and dimmed covers blend into the real (glow-tinted) background.
- In Settings, **A** now steps every choice row (theme, EQ preset, display mode, wallpaper) forward, not just some.
- Settings subtitle shortened to "Look, sound and controls".
- `tools/get_deps.sh` downloads atomically, checks that each file really is the expected header, retries, and
  accepts `MINIMP3_REF` / `STB_REF` / `DRLIBS_REF` to pin library versions.
- GitHub workflow: least-privilege permissions, cancel superseded runs, 15-minute timeout, fail if the `.3dsx` was
  not produced.
- README reorganised (contents, Favorites and Limits sections, accurate build instructions and source layout).

### Fixed
- **Build failed** (`implicit declaration` errors) because the neon redesign dropped `batt_poll`, `draw_card`,
  `draw_settings`, `draw_queue` and `draw_wallpaper_accents`; they are restored in `source/main.c`.
- **`make cia` aborted** because `BasicInfo/Title` in `resources/app.rsf` was 13 characters; makerom allows 8.
- **Repeat-one** replayed the playlist track instead of the song that had just finished when that song had been
  started from Up Next.
- **Removing a song from the Favorites list** jumped back to the top of the list; your place is now kept.
- **WAV files** with a zero channel count or block alignment could divide by zero in the decoder; they are now
  rejected as unplayable.
- The "empty heart" glyph (U+2661) is not guaranteed to exist in the 3DS system font; non-favorites now show a
  dimmed solid heart.
- The decoder-library configuration macros were duplicated and inconsistent between files; they now live in one
  header (`libcfg.h`) so every translation unit sees the same definitions.
- A stale, unused copy of `noise.h` (`slither.h`, from an earlier project name) was removed.
- **Lid behavior** now follows normal 3DS sleep handling: playback pauses on lid close and resumes on wake. The
  separate keep-playing setting and shell polling were removed.
- Shuffle / skip no longer slides the cover row past unloaded covers (grey cards); the playing view jumps straight
  to the new song and its artwork fades in from the background.
- Glow colour now eases over about a second instead of snapping.
- A grey frame behind the cover showed through while covers slid past.
- Settings and Favorites are written through temporary files before replacement, reducing the chance of losing
  the previous saved data after an interrupted SD-card write.
- Failed settings saves remain marked dirty and are retried instead of silently discarding unsaved changes.
- CIA/Home Menu publisher metadata now credits GRIMZZZZ.

### Internal
- Glow vertex colours are cached; the playing-song check no longer formats a path string per row per frame.
- Clean under `-Wall -Wextra -Wshadow` and the clang static analyzer; defensive NULL checks in the audio decoders.
- Removed a stray brace block in the EQ slider handler.
