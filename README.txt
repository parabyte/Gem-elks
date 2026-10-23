GEM for ELKS
============

This is a native FreeGEM/OpenGEM Desktop for ELKS on 8086/8088-class
machines.  This version is multi-application and uses ELKS kernel services
directly; it does not use the old pipe transport, a user-space scheduler, a
GEM memory arena, or a shadow filesystem.

The execution path is deliberately short:

  * GEM clients execute the original INT EFh AES/VDI bindings.  The ELKS
    kernel broker pins the caller's data segment, sleeps that task, and hands
    its register request to the resident /bin/gem process with gemctl().
  * ELKS vfork, _execve, waitpid, kill, and the kernel scheduler own process
    creation, lifetime, and CPU scheduling.
  * ELKS exec segments, malloc/free, and _fmemalloc/_fmemfree own memory.  GEM
    only keeps a six-entry channel-to-PID table and checked segment references.
  * Resource and Desktop I/O reaches ELKS open/read/write/stat/readdir and
    related filesystem calls.  The DOS-named Desktop functions only translate
    the original Desktop API and error codes; they are not a filesystem layer.
  * A 20 ms ELKS setitimer/SIGALRM drives AES timers and input polling.

Desktop remains alive when it launches an application.  ELKS schedules the
Desktop and applications concurrently, and the owner of the top GEM window
receives keyboard and mouse input.  The fixed table supports Desktop plus four
simultaneous applications/accessories (subject to ELKS RAM and task limits).

Kernel requirement
------------------

The project includes the required ELKS kernel/libc patch:

  patches/elks/0001-gemtrap-kernel-broker.patch

It adds the INT EFh broker, gemctl syscall, segment pinning, exec/exit records,
and working O_NONBLOCK IPv4 connect readiness for GemIRC.  Apply it to an ELKS
checkout and enable CONFIG_GEM_TRAP before building GEM.  A helper performs a
checked, repeatable apply:

  make -f Makefile.elks patch-elks ELKS_ROOT=/path/to/elks

The patch was integration-tested against ELKS master commit
052b4be698d9fb780ebdd9c9510ecdc4c2f9dd33.  The helper refuses to modify a
tree when git cannot apply the patch cleanly.

Build
-----

After enabling CONFIG_GEM_TRAP and rebuilding the ELKS kernel and libc:

  make -f Makefile.elks ELKS_ROOT=/path/to/elks -j8

The output is:

  build/bin/gem        resident AES/VDI owner
  build/bin/gemdesk    multi-application Desktop
  build/bin/gemclock   Clock
  build/bin/gemcalc    Calculator
  build/bin/gemview    HTTP browser (experimental)
  build/bin/gemirc     Antler IRC client

Install into an ELKS target tree with:

  make -f Makefile.elks ELKS_ROOT=/path/to/elks \
      DESTDIR=/path/to/elks/target install

See BUILDING.txt for the complete kernel, libc, image, and verification steps.

Run the IRC model checks on the host with:

  make -f Makefile.elks check-irc

Run
---

Start the resident owner; it registers INT EFh and starts Desktop itself:

  /bin/gem

Launch programs from Desktop normally.  Starting another GEM executable from
an ELKS shell while /bin/gem is active also works: APPL_INIT adopts that real
ELKS task into a free GEM channel.

GemIRC starts with a connection dialog:

  /bin/gemirc

It can also connect from command-line values:

  /bin/gemirc server channel nick port

The Antler client keeps separate server, channel, and query buffers, a sorted
nick list, history, completion, menus, scrolling, and reconnect state.  Its
transport calls ELKS sockets directly.

Resources and Desktop configuration are installed in /lib/gemsys.  The mouse
defaults to a Microsoft serial mouse on /dev/ttyS0.  MOUSE_PORT selects another
device (or "none"), MOUSE_PROTOCOL selects "ms", "ps2", or "amstrad", and
CONSOLE selects the keyboard tty.

Compatibility limits
--------------------

PROC_INFO, PROC_MALLOC, PROC_MFREE, and PROC_SETBLOCK are unavailable.  ELKS
owns process memory.  Event tapes and client interrupt-vector callbacks are
also unavailable.  Normal AES timers use the ELKS interval timer directly.
