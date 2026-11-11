# PROJECT NOISE - Neon UI

Default theme is **Neon** (black, neon green `#24FF68`); the other themes recolour the same layout.

## Bottom screen (`source/main.c`)
- Thin neon outline around the screen; faint wallpaper accents behind it (Settings > Appearance).
- Top row: **All Songs / Liked Songs / Queue** tabs (Queue opens Up Next). Full-width search pill under them
  (tap to search, x to clear).
- Song list: four 25 px rows with a coloured tile, title/artist, a drawn heart (tap to like) and a neon
  selection outline. Animated bars mark the playing song.
- Now-playing deck: seek bar with elapsed/total time, title/artist, shuffle, previous, play/pause,
  next, repeat and Up Next (with a queued-songs badge).
- Short status toasts appear just above the deck.

## Layering
citro2d depth: higher z is in front, equal z follows draw order, text is always z 0.5.
0.05 gradient, 0.08 wallpaper, 0.10 frame, 0.20 panels, 0.22 panel insets, 0.30 widgets, 0.45 icons, 0.46 toast.
