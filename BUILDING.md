# Building and installing GEM for ELKS

## Requirements

- a **stock** ELKS source tree with its normal GNU ia16 cross toolchain
  built (no kernel patches or config options are needed);
- GNU make, `sha256sum`, and standard POSIX host tools.

The ELKS source tree is external.  This repository does not vendor ELKS or
a cross compiler.

## Native 8086 build

From this repository, run:

```sh
make -f Makefile.elks ELKS_ROOT=/path/to/elks -j8 all
make -f Makefile.elks ELKS_ROOT=/path/to/elks audit
```

`ELKS_ROOT` supplies:

```text
cross/bin/ia16-elf-gcc
cross/bin/ia16-elf-ar
cross/bin/ia16-elf-objdump
elks/tools/bin/objdump86
elks/tools/bin/elf2elks
libc/include
elks/include
```

The build fixes `-march=i8086`, uses ELKS's medium code model with the
`regparmcall` convention, and keeps data near.  It does not require an
80186 instruction, protected mode, an FPU, or 32-bit compiler arithmetic.

The release model gates report these bounds with the qualified ELKS
toolchain at the default `-Os`:

| Program | Near text | Far text | Data plus BSS | Heap | Stack |
| --- | ---: | ---: | ---: | ---: | ---: |
| `gem` | 41,072 | 49,568 | 24,160 | 4,096 | 2,048 |
| `gemdesk` | 32,688 | 26,080 | 45,440 | 12,800 | 2,048 |

All values are unscaled bytes.  The link fails its post-link gate if a
16-bit ELKS segment limit is crossed.

## Staged installation

To populate an ELKS filesystem staging directory:

```sh
make -f Makefile.elks ELKS_ROOT=/path/to/elks \
    DESTDIR=/path/to/elks-image-root install
```

This installs exactly:

```text
/bin/gem
/bin/gemdesk
/etc/DESKTOP.INF
/GEMAPPS/GEMSYS/DESKTOP.RSC
/GEMAPPS/GEMSYS/DESKHI.ICN
/GEMAPPS/GEMSYS/DESKLO.ICN
```

Start `/bin/gem`; it spawns `/bin/gemdesk` itself with the transport pipes
already in place.  See `docs/ELKS-INTEGRATION.md` for the optional
`buildext.sh` external-application hookup.

## Audit targets

- `source-audit` rejects floating types, wide target arithmetic, and
  non-8086 source patterns.
- `asset-audit` validates the four original resource files byte for byte.
- `codegen-audit` disassembles every retained object and rejects
  80186-plus, x87, and 32-bit operand instructions.  MUL and DIV are
  ordinary 8086 instructions and are permitted, matching upstream ELKS
  review guidance.
- `lineage-audit` measures functions retained by the final Desktop link.
  Its manifests predate the stock-ELKS port and are being rebaselined.

Run `make clean` to remove all generated files.
