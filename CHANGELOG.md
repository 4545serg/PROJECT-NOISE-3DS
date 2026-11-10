# Changelog

## Unreleased

### Added
- **Up Next screen**: the mini-player queue button opens a dedicated queue view with song titles, folder context, immediate play, per-item removal and clear-all controls.
- **Manual play queue**: press Y on a selected song to add it to the end of Up Next, or X to prioritize it as the next track; queue-screen X/Y shortcuts are non-destructive.
- **Sleep timer** (5 / 10 / 15 / 30 / 45 / 60 minutes) in Settings > General, with a live countdown, start/reset and cancel controls.
- **Favorites** library section with per-song heart toggles; favorites persist on SD and can be shuffled as a playlist.
- **Cover fade** setting (Quick / Smooth / Slow, Settings > Appearance): the now-playing cover and glow fade in softly when a song starts.
- **Ambient glow**: the top-screen background takes on the dominant colour of the album art around the centre
  cover, easing smoothly between covers (Settings > Appearance > Ambient glow: Off / Soft / Vivid).
- **Skip length** setting (5 / 10 / 15 / 30 s) for the D-pad skip.
- Transport buttons flash briefly when pressed.

### Changed
- Refined the top-screen now-playing layout with more breathing room around track metadata and the progress bar; surfaced the queue count in the status area.
- Settings tabs are now **Appearance**, **Audio** and **General**; removed the D-pad/B help footer and the visible lid-playback option.
- Removed the in-app volume control; DSP output is kept at unity gain so the physical 3DS volume slider controls loudness.
- Softened the ambient lighting and top-screen background wash for more restrained contrast.
- **D-pad Left/Right** now skip back / forward; **L / R** now play the previous / next song.
- Skipping a song shows what is playing and moves the list highlight to it.
- First-run setup shortened from six steps to three; theme, glow, clock and battery share one page.
- Simplified Appearance by removing the separate accent-colour picker; each theme now supplies its own coordinated colours.
- Improved Up Next guidance: larger, lower-positioned instructions clearly explain Y to queue and X to prioritize a song.
- Cover-flow reflections and dimmed covers blend into the real (glow-tinted) background.

### Fixed
- **Lid behavior** now follows normal 3DS sleep handling: playback pauses on lid close and resumes on wake. The separate keep-playing setting and shell polling were removed.
- Shuffle / skip no longer slides the cover row past unloaded covers (grey cards); the playing view jumps straight to the new song and its artwork fades in from the background.
- Glow colour now eases over about a second instead of snapping.
- A grey frame behind the cover showed through while covers slid past.

### Internal
- Glow vertex colours are cached; the playing-song check no longer formats a path string per row per frame.
- Clean under `-Wall -Wextra -Wshadow` and the clang static analyzer; defensive NULL checks in the audio decoders.


## Stability update
- Settings and Favorites are written through temporary files before replacement, reducing the chance of losing the previous saved data after an interrupted SD-card write.
- Failed settings saves remain marked dirty and are retried instead of silently discarding unsaved changes.
- CIA/Home Menu publisher metadata now credits GRIMZZZZ.
