# Changelog

## Unreleased

### Added
- **Sleep timer** (5 / 10 / 15 / 30 / 45 / 60 minutes) in Settings > General, with a live countdown, start/reset and cancel controls.
- **Favorites** library section with per-song heart toggles; favorites persist on SD and can be shuffled as a playlist.
- **Cover fade** setting (Quick / Smooth / Slow, Settings > Look): the now-playing cover and glow fade in softly when a song starts.
- **Ambient glow**: the top-screen background takes on the dominant colour of the album art around the centre
  cover, easing smoothly between covers (Settings > Look > Ambient glow: Off / Soft / Vivid).
- **Skip length** setting (5 / 10 / 15 / 30 s) for the D-pad skip.
- Transport buttons flash briefly when pressed.

### Changed
- **D-pad Left/Right** now skip back / forward; **L / R** now play the previous / next song.
- Skipping a song shows what is playing and moves the list highlight to it.
- First-run setup shortened from six steps to three; theme, accent, glow, clock and battery share one page.
- Cover-flow reflections and dimmed covers blend into the real (glow-tinted) background.

### Fixed
- **Closed-lid playback** now uses APT sleep hooks to pause/resume correctly when lid playback is disabled, and polls shell state when enabled. Rendering is suspended and the UI thread idles while closed to reduce battery use.
- Shuffle / skip no longer slides the cover row past unloaded covers (grey cards); the playing view jumps straight to the new song and its artwork fades in from the background.
- Glow colour now eases over about a second instead of snapping.
- A grey frame behind the cover showed through while covers slid past.

### Internal
- Glow vertex colours are cached; the playing-song check no longer formats a path string per row per frame.
- Clean under `-Wall -Wextra -Wshadow` and the clang static analyzer; defensive NULL checks in the audio decoders.


## Listening and appearance polish

- Added configurable short/gentle audio fade-in on track start and fade-out when playback is stopped.
- Added a live PCM-level visualizer for the top screen, with Carousel, Visualizer, and Both display modes.
- Renamed the settings tabs to Appearance, Audio, and General, and tightened the settings header and row layout.
- Added compact controls for the top display mode and track fades; both preferences are saved persistently.
