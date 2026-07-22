# Adding GEM to ELKS disk images (stock kernel)

GEM integrates with ELKS exactly like the other optional external
applications (Nano-X/Microwindows, D-Flat, Doom): through `buildext.sh` and
the `elkscmd/ExtApplications` install list.  **No kernel change, kernel
config option, or patch is involved** — the previous `CONFIG_GEM_TRAP`
broker requirement is gone.

## buildext.sh

Add an `elksgem` function alongside the other external apps and call it
from `make_all`:

```sh
elksgem()
{
    ELKSGEM_TAG=v2026.07.22-stock.1

    echo "Building GEM Desktop for ELKS..."
    cd $TOPDIR/extapps
    if [ ! -d elks-gem ] ; then
        git clone --branch $ELKSGEM_TAG --depth 1 \
            https://github.com/parabyte/Gem-elks.git elks-gem
    fi
    cd elks-gem
    git fetch --depth 1 origin \
        refs/tags/$ELKSGEM_TAG:refs/tags/$ELKSGEM_TAG
    git checkout --detach $ELKSGEM_TAG
    make -f Makefile.elks clean
    make -f Makefile.elks ELKS_ROOT=$TOPDIR
    echo "GEM Desktop build complete"
}
```

## elkscmd/ExtApplications

```text
#   :elksgem    GEM Desktop files for hard disk images
elks-gem/build/bin/gem          ::bin/gem           :elksgem
elks-gem/build/bin/gemdesk      ::bin/gemdesk       :elksgem
elks-gem/assets/GEMAPPS/GEMSYS/DESKTOP.INF ::etc/DESKTOP.INF            :elksgem
elks-gem/assets/GEMAPPS/GEMSYS/DESKTOP.RSC ::GEMAPPS/GEMSYS/DESKTOP.RSC :elksgem
elks-gem/assets/GEMAPPS/GEMSYS/DESKHI.ICN  ::GEMAPPS/GEMSYS/DESKHI.ICN  :elksgem
elks-gem/assets/GEMAPPS/GEMSYS/DESKLO.ICN  ::GEMAPPS/GEMSYS/DESKLO.ICN  :elksgem
```

## elkscmd/config.in

The desktop is an ordinary application choice; it needs only an IBM PC
real-mode target:

```text
comment 'Graphical desktop'

if [ "$CONFIG_ARCH_IBMPC" = "y" ]; then
    if [ "$CONFIG_286_PMODE" != "y" ]; then
        bool 'GEM Desktop (hard disk only)' CONFIG_APP_GEM n
    else
        comment '(GEM Desktop needs an 8086 real-mode kernel)'
    fi
else
    comment '(GEM Desktop is available for IBM PC only)'
fi
```

## elkscmd/Make.install

```makefile
ifdef CONFIG_APP_GEM
    ifneq ($(CONFIG_ARCH_IBMPC), y)
        $(error CONFIG_APP_GEM requires CONFIG_ARCH_IBMPC=y)
    endif
    ifeq ($(CONFIG_286_PMODE), y)
        $(error CONFIG_APP_GEM is not available in protected mode)
    endif
    TAGS += :elksgem|
endif
```

## Runtime

Boot the image and run `/bin/gem`.  The server opens the display, spawns
`/bin/gemdesk` with the transport pipes on descriptors 3 and 4, and serves
AES/VDI requests until the Desktop quits.  Launching a program from the
Desktop suspends graphics, runs it on the text console, and returns to a
fresh Desktop.

Environment knobs (all optional) are read by the server's input layer:
`CONSOLE` selects the keyboard tty, `MOUSE_PORT` the mouse device (default
`/dev/ttyS0`, or `none`), and `MOUSE_PROTOCOL` selects `ms` or `ps2`.
