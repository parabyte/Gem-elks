# Building and installing GEM for ELKS

## Requirements

- an ELKS source tree with its normal GNU ia16 cross toolchain built;
- GNU make, Perl, `sha256sum`, and standard POSIX host tools; and
- an ELKS kernel tree containing the GEM trap and graphics-ownership support.

The source tree is external.  This repository does not vendor ELKS or a cross
compiler.

## Native 8086 build

From this repository, run:

```sh
make -f Makefile.elks ELKS_ROOT=/path/to/elks -j2 all
make -f Makefile.elks ELKS_ROOT=/path/to/elks audit
```

`ELKS_ROOT` supplies:

```text
cross/bin/ia16-elf-gcc
cross/bin/ia16-elf-ar
cross/bin/ia16-elf-objdump
elks/tools/bin/objdump86
libc/include
elks/include
```

The build fixes `-march=i8086`, uses ELKS's medium code model, keeps data near,
and permits far calls only where the linked code exceeds one 64 KiB text
segment.  It does not require an 80186 instruction, protected mode, an FPU, or
32-bit compiler arithmetic.

The release model gates report these bounds with the qualified ELKS toolchain:

| Program | Near text | Far text | Data plus BSS | Heap | Stack |
| --- | ---: | ---: | ---: | ---: | ---: |
| `gemaes` | 65,072 | 63,216 | 25,296 | 128 | 1,024 |
| `gemdesk` | 46,752 | 39,568 | 46,368 | 12,800 | 2,048 |

All values are unscaled bytes.  The link fails its post-link gate if a 16-bit
ELKS segment limit is crossed.

## Staged installation

To populate an ELKS filesystem staging directory:

```sh
make -f Makefile.elks ELKS_ROOT=/path/to/elks \
    DESTDIR=/path/to/elks-image-root install
```

This installs exactly:

```text
/bin/gemaes
/bin/gemdesk
/GEMAPPS/GEMSYS/DESKTOP.RSC
/GEMAPPS/GEMSYS/DESKTOP.INF
/GEMAPPS/GEMSYS/DESKHI.ICN
/GEMAPPS/GEMSYS/DESKLO.ICN
```

The official ELKS external-application integration performs the same staged
copy for hard-disk images.  Start `gemaes` first; `gemdesk` waits for the real
trap signature before entering the original `GEMAIN` path.

## Audit targets

- `source-audit` rejects floating types, wide target arithmetic, and non-8086
  source patterns.
- `asset-audit` validates the four original resource files byte for byte.
- `codegen-audit` disassembles every retained object and rejects 80186-plus,
  x87, 32-bit operand, multiply, and divide instructions.
- `lineage-audit` measures only functions retained by the final Desktop link
  and rejects duplicate strong project symbols.

Run `make clean` to remove all generated files.
