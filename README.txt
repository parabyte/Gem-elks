GEM for ELKS
============

A native FreeGEM/OpenGEM Desktop for stock ELKS:

  gem       the AES/VDI server and single-tasking shell
  if you run gem without any arguments it will spawn gemdesk



Config lives in /etc/DESKTOP.INF, read at startup and rewritten by
"Save Desktop".  The other original assets (DESKTOP.RSC, DESKHI.ICN,
DESKLO.ICN) live in /GEMAPPS/GEMSYS.

Build
-----

Point the build at a stock ELKS tree that already has its ia16
toolchain built:

  make -f Makefile.elks ELKS_ROOT=/path/to/elks -j8

You get:

  build/bin/gem        AES/VDI server and shell
  build/bin/gemdesk    GEM Desktop
  build/bin/gemclock   Clock
  build/bin/gemcalc    Calculator
  build/bin/gemview    web browser
  build/bin/gemirc     IRC client

make install DESTDIR=... puts the binaries in /bin, the config in
/etc, and the resources in /GEMAPPS/GEMSYS.  

Run
---

  /bin/gem

gem starts the Desktop by itself.

The mouse is a Microsoft serial mouse on /dev/ttyS0 by default.
MOUSE_PORT picks another device (or "none"), and MOUSE_PROTOCOL
picks "ms", "ps2", or "amstrad" - the last one reads the mouse port
built into the Amstrad PC1512/PC1640 system board, no serial port
involved.  CONSOLE picks the keyboard tty.


Running one application directly:

  gem program-name [arguments...]



Web browser:

sorry everyone this is really broken, its a moving target!

do not expect it to work, sorry

  gem /bin/gemview

Type a URL or a search into the address bar and press Return.  Plain
HTTP only (no TLS); it follows links and redirects, submits simple
forms, and keeps a short Back history.

IRC client:

  gem /bin/gemirc [server [channel [nick [port]]]]



