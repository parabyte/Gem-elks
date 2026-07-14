# GEM for ELKS

This repository contains a native FreeGEM/OpenGEM Desktop port for ELKS on
IBM PC/XT-class 8088 and 8086 systems.  It builds two ELKS executables:

- `gemaes`, the resident AES/VDI owner; and
- `gemdesk`, the original GEM Desktop client with narrow ELKS/POSIX seams.

The Desktop reads the original `DESKTOP.RSC`, `DESKHI.ICN`, `DESKLO.ICN`, and
`DESKTOP.INF` files directly.  There is no resource conversion step, hosted UI
layer, DOS command shell, or runtime compatibility wrapper.  GEM applications
use the native 8086 `INT EFh` array ABI supplied by the ELKS GEM broker.

## Build

Use an ELKS tree with its GNU ia16 toolchain already built:

```sh
make -f Makefile.elks ELKS_ROOT=/path/to/elks -j2
make -f Makefile.elks ELKS_ROOT=/path/to/elks audit
```

The build produces:

```text
build/bin/gemaes
build/bin/gemdesk
```

`make audit` verifies source constraints, the original asset hashes, the ELKS
memory model, 8086-only code generation, absence of compiler-generated wide
arithmetic, duplicate strong symbols, and the measured original-GEM lineage.
See [BUILDING.md](BUILDING.md) for the toolchain and installation details.

## Runtime layout

Install the two programs in `/bin` and the four assets in
`/GEMAPPS/GEMSYS`.  Start `/bin/gemaes` before `/bin/gemdesk`.  The ELKS kernel
must provide the GEM trap broker and task-owned graphics access; the official
ELKS integration keeps the package disabled unless that kernel support is
selected.

The runtime uses ELKS kernel tasks, address spaces, `vfork`/`exec`, signals,
and file descriptors for process and memory ownership.  Desktop file commands
map directly to bounded POSIX system calls.  All target-side arithmetic uses
8-bit or 16-bit values and explicit word pairs at unavoidable ABI boundaries;
no floating point or 32-bit C arithmetic is used.

## Scope

This clean core release deliberately contains no ELKS vendor tree, disk image,
generated binary, bulk reference-source snapshot, or DRI Toolkit Clock-derived
code.  It contains only the source and original runtime assets needed for the
native AES owner and Desktop client.

The measured core lineage is 91.35% original or direct-derived GEM source after
excluding only the three explicit ELKS/POSIX boundary modules.  The complete
client metric, which includes those unavoidable boundaries, is also reported
by the reproducible audit.  See [docs/LINEAGE.md](docs/LINEAGE.md).

## License

The project is distributed under GNU GPL version 2 only.  Historical Digital
Research and Caldera notices are retained in their source files.  See
[LICENSE](LICENSE) and [NOTICE](NOTICE).
