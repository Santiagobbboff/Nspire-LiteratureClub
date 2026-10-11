# Credits and source notices

This port modifies the nZATO runtime/converter for DDLC Act 1. Port contributors:
**Santiagobbboff**. Local release-preparation modifications dated
**2026-10-10** include DDLC file naming, pack-owned UI images, local archive
extraction/conversion, dialogue lookups and removal of unused upstream pipelines.

## nZATO

Original project: **zyf722**, <https://github.com/zyf722/nZATO>.
Runtime and host encoder are derived from nZATO, licensed under GNU GPL version 3.
The full license is retained in `LICENSE`. Modified runtime/build and converter
sources are included in this source tree. 

## DDLC TI-84 Plus CE

Reference project: **koboshchan**, <https://github.com/koboshchan/ddlc-ti84plusce>.
Its MIT-licensed tools are incorporated as follows:

- `tools/rpyc_ast.py`: compiled Ren'Py script reader.
- `tools/ddlc_image_resolve.py`: adapted image resolver from `tools/image_resolve.py`.
- `tools/ddlc_transforms.py`: adapted transform-table reader from `tools/compile_script.py`.

Full copyright/permission notice and upstream disclaimer:
`licenses/ddlc-ti84plusce-LICENSE.txt`.

## nvid2

Original TI-Nspire Xvid/MPEG-4 Part 2 decoder adaptation:
**giraf-fe**, <https://github.com/giraf-fe/nvid2>.

nvid2 is licensed under GNU GPL version 2 or, at your option, any later version
(GPL-2.0-or-later). Its adapted decoder reached this port through
Nspire-VideoPlayer-Ndless and nZATO. The provenance is documented in nZATO's
`docs/architecture.md` and `docs/opening-video.md`.

The shared GPLv2 license text supplied with nZATO's release is retained at
`licenses/Xvid-and-nvid2.txt`. Existing Xvid copyright/license headers remain
beside the vendored source. This attribution supplements the intermediate
project's notice below.

## Nspire-VideoPlayer-Ndless

Intermediate video-player/NVP implementation by **GigaZelensky**,
<https://github.com/GigaZelensky/Nspire-VideoPlayer-Ndless>.
Its retained GPLv3 notice is at
`cpp/third_party/nvp/LICENSE-Nspire-VideoPlayer-Ndless`. Decoder files and the
vendored Xvid sources remain available with their original notices.

## Xvid

Xvid authors are identified in each retained source header. The headers license
the relevant sources under GNU GPL version 2 or later. The supplied license text
is retained in `cpp/third_party/nvp/xvid/LICENSE`.

## Original DDLC

Original game by **Team Salvato**, <https://ddlc.moe>.
Original game assets, dialogue and fonts are not licensed under this port's engine
license. Users supply a local original installation to generate their private pack.
These credits do not imply endorsement or permission from the original creators.
