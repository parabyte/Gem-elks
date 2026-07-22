# GEM for ELKS

This repository contains a native FreeGEM/OpenGEM Desktop port for **stock
ELKS** on IBM PC/XT-class 8088 and 8086 systems.  It requires **no kernel
modification of any kind**: no new system calls, no interrupt broker, no
kernel config options, no patches.  It builds two ordinary ELKS
executables:

- `gem`, the AES/VDI server and single-tasking shell; and
- `gemdesk`, the original GEM Desktop client, started automatically by
  `gem`.

## Architecture

Following upstream ELKS guidance, the AES/VDI owner is an ordinary user
process.  `gem` owns the screen and input devices, spawns the Desktop with
two ordinary kernel pipes on file descriptors 3 and 4, and serves the
original 22-byte INT EF register records the client writes.  On a real-mode
machine the resident AES/VDI reads and writes the client's memory directly
through the recorded segment words, so nothing is copied or converted in
between.

- A blocking pipe read replaces the kernel INT EF wait.
- A 20 ms `select()` timeout replaces the SIGALRM input tick, so no signal
  handler exists anywhere (and no medium-model signal delivery is needed).
- A closed pipe replaces the kernel EXIT record.

The ELKS kernel owns everything it normally owns:

- **Processes.**  The GEM/XM logical process table, DOS arena records, and
  channel bookkeeping are gone.  Program launch is original single-tasking
  GEM: `SHEL_WRITE` records the command, the Desktop exits, and `gem` runs
  the program with plain `vfork`/`execv`/`waitpid` on the restored text
  console, then starts a fresh Desktop.
- **Memory.**  `dos_alloc`/`dos_free` are direct `malloc`/`free`; far
  resource segments come from the stock `_fmemalloc` system call.
- **Filesystem.**  Every `dos_*` call reaches its ELKS system call
  directly (`open`, `read`, `write`, `mkdir`, `rename`,
  `opendir`/`readdir`, ...).  The emulated INT 21 register machine
  (`__DOS()`) has been removed.

## Configuration

GEM configuration lives in the standard ELKS `/etc` directory:
`/etc/DESKTOP.INF` is read at startup and rewritten by "Save Desktop".
The remaining original assets (`DESKTOP.RSC`, `DESKHI.ICN`, `DESKLO.ICN`)
are resources, not configuration, and stay in `/GEMAPPS/GEMSYS`.

## Build

Use a stock ELKS tree with its GNU ia16 toolchain already built:

```sh
make -f Makefile.elks ELKS_ROOT=/path/to/elks -j8
make -f Makefile.elks ELKS_ROOT=/path/to/elks audit
```

The build produces:

```text
build/bin/gem
build/bin/gemdesk
```

`make install DESTDIR=...` installs `/bin/gem`, `/bin/gemdesk`,
`/etc/DESKTOP.INF`, and the `/GEMAPPS/GEMSYS` resources.  See
[BUILDING.md](BUILDING.md) and
[docs/ELKS-INTEGRATION.md](docs/ELKS-INTEGRATION.md) for adding GEM to ELKS
disk images as an optional external application.

## Run

```sh
/bin/gem
```

`gem` starts the Desktop itself.  Quitting the Desktop returns to the
shell; launching a program suspends graphics, runs it full screen, and
returns to a fresh Desktop, exactly like single-tasking GEM on DOS.

## Scope

This release deliberately contains no ELKS vendor tree, kernel patch, disk
image, generated binary, or DRI Toolkit Clock-derived code.  It contains
only the source and original runtime assets needed for the AES/VDI server
and the original Desktop client.  See [docs/LINEAGE.md](docs/LINEAGE.md).

## License

The project is distributed under GNU GPL version 2 only.  Historical Digital
Research and Caldera notices are retained in their source files.  See
[LICENSE](LICENSE) and [NOTICE](NOTICE).
