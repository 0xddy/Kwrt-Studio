# Third-party components

The main application is native C++/Win32. The following upstream components are included unchanged, except for being placed in the portable package directory:

| Component | Source / license |
|---|---|
| nlohmann/json 3.11.3 | https://github.com/nlohmann/json/releases/tag/v3.11.3 — MIT; source and notice in `vendor/json.hpp` |
| squashfs-tools-ng 1.3.2 Windows x64 tools and libraries | https://infraroot.at/pub/squashfs/windows/squashfs-tools-ng-1.3.2-mingw64.zip — see `licenses/squashfs-tools-ng-COPYING.md` |
| libsquashfs | LGPL-3.0-or-later, with separately licensed components; see upstream notices |
| sqfs2tar / tar2sqfs | GPL-3.0-or-later, with separately licensed components; see upstream notices |
| XZ / LZMA 5.2.5, LZO 2.10, LZ4 1.9.4, zstd 1.5.2, zlib 1.2.12 | Upstream corresponding source archives are included in `third-party-source/`; see each archive and `licenses/` for notices |

The squashfs-tools-ng source archive is also included in `third-party-source/`, along with bzip2 1.0.8 source. Upstream archives retain build scripts and license notices. The main application launches the separate native SquashFS tools through standard input/output; it does not require a shell, Python, Docker, WSL, or network access.

The contents of input and output router firmware retain their respective upstream licenses. Firmware packages are not incorporated into the converter's source code. Personal `profile.json` and generated firmware contain user-specific network settings; they are not part of the reusable application source.
