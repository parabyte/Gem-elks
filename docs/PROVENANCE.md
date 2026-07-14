# Source and asset provenance

## FreeGEM and OpenGEM Desktop

The active Desktop is based on the GPL-released FreeGEM/OpenGEM Desktop 3.2.4
family.  Historical notices remain in every imported source file.

- OpenGEM repository: `https://github.com/shanecoughlan/OpenGEM`
- pinned OpenGEM revision: `ac06b1a3fec3f3e8defcaaf7ea0338c38c3cef46`
- SeaSIP Desktop archive: `ppddesk3.2.4.zip`
- archive SHA-256:
  `72e82a86f04c1c78b7725aa5b956d8fbf3aa34c1853f0f73f761941366f6464c`

The ELKS port retains the original Desktop function structure.  Target changes
are limited to native POSIX file/process calls, explicit offset/segment word
pairs, bounded memory ownership, and 8086-cheap arithmetic.

## Pacific C GEM bindings

The native `INT EFh` binding closure came from the OpenGEM Pacific C bindings
and the separately pinned SeaSIP multi-application extension.  The detailed
archive hashes, imported file sets, and normalized baselines are retained in
`src/bindings/original/readme.md`; its GPL version 2 text remains beside it as
`src/bindings/original/license.txt`.

## Original runtime assets

These are parsed directly at runtime without source conversion:

| Asset | SHA-256 |
| --- | --- |
| `DESKHI.ICN` | `04bd6ff0f60f5893f842b44503fa3c638cf0d0cd413e3b6613cb2945e570f555` |
| `DESKLO.ICN` | `ec9547e8c189ccf8c00d6846be4a443ef1f6c5d30374fced1b33ac2595c9dbba` |
| `DESKTOP.INF` | `a3786b52efc40c5676844d2673c4070cb4720e168a37e3450c464f84bd227dc5` |
| `DESKTOP.RSC` | `d93bfbaace80d3188f8743f5bbca2ecd6279dbef36180a69b6dc1a8baf86826b` |

The source release does not include alternate runtime copies, generated
resource code, screenshots, disk images, or bulk reference archives.

## Explicit exclusions

This repository excludes the DRI GEM Toolkit Clock sample and all derivatives
because its historical transfer notice is not the FreeGEM GPL grant.  It also
excludes optional applications which are not required by the two-program core,
the patched ELKS deployment tree, test disk images, and generated build output.
