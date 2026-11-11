# Changelog

## Unreleased

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
