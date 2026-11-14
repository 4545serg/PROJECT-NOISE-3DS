# PROJECT NOISE - Neon UI

Default theme is **Noise** (near-black purple `#08030F`, soft white `#F4EEFF`, electric violet `#A020FF`); the other themes recolour the same layout. Branding uses the crown / chain / purple-flame artwork and the creator credit **MADE BY GRIMZZZZ**.

Brand assets in `resources/`:
- `icon.png`: 48x48 3DS app icon
- `banner.png`: 256x128 HOME Menu banner
- `cover.png`: 128x128 matching fallback cover
- `boot_top.png`: 800x480 top-screen boot artwork (4:3 pixel-doubled 400x240)
- `boot_bottom.png`: 640x480 bottom-screen boot artwork (pixel-doubled 320x240)

`boot_top.png` / `boot_bottom.png` are kept as reference artwork and are not loaded at runtime (a full-screen texture would cost startup time and memory on an Old 3DS). The in-app boot screen is drawn with citro2d from the embedded branded cover, so it adds no assets and no dependencies.

## Settings
Seven sections - Playback, Audio, Display, Background Playback, Timers, Library, About - chosen with a rounded switcher bar (tap left / right half) with page dots beside the title. See `README.md` for which rows live where.

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
