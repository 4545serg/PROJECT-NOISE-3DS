# PROJECT NOISE — Neon UI redesign

The UI has been restyled to closely follow the supplied PROJECT NOISE reference screens.

## Top screen
- Black OLED-style background with a bright neon-green rounded outer frame.
- Compact pill-shaped PROJECT NOISE header with signal/activity bars.
- Date/time pill on the right.
- Large centered album-art panel with a bright green keyline and layered glow.
- Large centered song title beneath the artwork.
- Thick neon-green seek bar with a circular handle.
- Existing artwork loading/fade behavior and cover-flow behavior are retained.

## Bottom screen
- Matching neon-green rounded frame on black.
- Home button, search field, and Liked Songs shortcut across the top.
- Three pill-shaped library tabs: All Songs, Liked Songs, Playlists.
- Compact song rows with artwork tiles, title/artist text, duration/favorite indicators, and a bright selection outline.
- Neon scrollbar.
- Large bottom playback deck closely matching the supplied reference: Now Playing panel, shuffle/previous/play/pause/next/repeat transport panel, and a compact speaker/volume panel.

## Interaction/layout updates
- Touch hit areas were moved to follow the new visual layout.
- List rows were compressed to leave room for the new playback deck.
- Existing physical-button behavior, favorites, search, queue, playback, and settings logic remain in place.

The redesign is implemented in `source/main.c`, which is the file compiled by the project's Makefile.
