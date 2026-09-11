# Third-party components

The following upstream components are bundled with Kwrt Studio:

| Component | Source / license |
|---|---|
| nlohmann/json 3.11.3 | https://github.com/nlohmann/json/releases/tag/v3.11.3 — MIT; source and notice in `vendor/json.hpp` |
| libssh2 1.11.1 | https://github.com/libssh2/libssh2/releases/tag/libssh2-1.11.1 — BSD-3-Clause; `licenses/libssh2-COPYING.txt`, corresponding source in `third-party-source/libssh2-1.11.1.tar.xz` |
| squashfs-tools-ng 1.3.2 Windows x64 tools and libraries | https://infraroot.at/pub/squashfs/windows/squashfs-tools-ng-1.3.2-mingw64.zip — see `licenses/squashfs-tools-ng-COPYING.md` |
| libsquashfs | LGPL-3.0-or-later, with separately licensed components; see upstream notices |
| sqfs2tar / tar2sqfs | GPL-3.0-or-later, with separately licensed components; see upstream notices |
| XZ / LZMA 5.2.5, LZO 2.10, LZ4 1.9.4, zstd 1.5.2, zlib 1.2.12 | Upstream corresponding source archives are included in `third-party-source/`; see each archive and `licenses/` for notices |

The squashfs-tools-ng and bzip2 1.0.8 source archives are also included in `third-party-source/`. All upstream archives retain their build scripts and license notices. The application communicates with the separate SquashFS tools through standard input/output.

Input and output firmware retain their respective upstream licenses. Firmware packages are not included in this repository.

libssh2 is built statically with the Windows CNG backend and ECDSA enabled. The source archive SHA-256 is `9954cb54c4f548198a7cbebad248bdc87dd64bd26185708a294b2b50771e3769` and is checked at configure time.
