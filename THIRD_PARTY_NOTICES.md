# Third-party software

PROJECT SLITHER's own code (everything in `source/` except the two vendored
headers below, plus `Makefile`, `tools/`, `resources/`) is MIT licensed - see `LICENSE`.
No code was copied from other 3DS music players; the app was written against
the libraries below.

| Component | Used for | License | Source |
|---|---|---|---|
| minimp3 (`minimp3.h`) | MP3 decoding | CC0 (public domain dedication) | https://github.com/lieff/minimp3 |
| stb_image (`stb_image.h`) | JPEG/PNG album art decoding | Public domain / MIT (dual) | https://github.com/nothings/stb |
| libctru | 3DS system services, ndsp audio, input | zlib | https://github.com/devkitPro/libctru |
| citro2d / citro3d | 2D rendering and text | zlib | https://github.com/devkitPro/citro2d , https://github.com/devkitPro/citro3d |
| devkitARM / devkitPro tools | Toolchain, 3dsxtool, smdhtool | Various (GPL with linking exceptions for the runtime) | https://devkitpro.org |

`minimp3.h` and `stb_image.h` are not stored in this repository; `tools/get_deps.sh`
downloads them at build time and each file carries its own license text in its header.

Build-time-only tools for the CIA (not linked into the app): makerom
(https://github.com/3DSGuy/Project_CTR) and bannertool (https://github.com/Steveice10/bannertool).

The generated icon, banner and banner jingle in `resources/` are original,
produced by `tools/make_assets.py`.
