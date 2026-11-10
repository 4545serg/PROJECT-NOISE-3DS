# Changelog

## Unreleased

### Added
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
- A grey frame behind the cover showed through while covers slid past.

### Internal
- Glow vertex colours are cached; the playing-song check no longer formats a path string per row per frame.
- Clean under `-Wall -Wextra -Wshadow` and the clang static analyzer; defensive NULL checks in the audio decoders.
