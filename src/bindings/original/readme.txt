Original GEM Pacific C bindings with ELKS seams
===============================================

This directory is the linked binding closure for the native OpenGEM
Desktop client.  It started as a normalized import of the original
GEM AES and VDI Pacific C bindings and keeps their function-level
trap flow.

Where it came from
------------------

The main binding source is the OpenGEM 7 RC1 SDK at revision
ac06b1a3fec3f3e8defcaaf7ea0338c38c3cef46:

  source/OpenGEM-7-RC1-SDK.zip
  OpenGEM-7-SDK/PROGRAMMING BINDINGS AND COMPILERS/
  GEM Developers Kit for Pacific C (Pacific C bindings)/LIBSRC

  archive Git blob:  fc43118a005e08c7cbe5c2c7c7cb6f421755326b
  archive SHA-256:
    9bec979807f1f3247e56647e50d068f39f6670900129b8ae82874924daf6023d
  archive size:      19,006,381 bytes

The multi-application entries come from the separately pinned SeaSIP
Pacific C GEM Developers Kit:

  archive:           ppd_gdk.zip
  archive SHA-256:
    465342231610a0244f585f0107ee56dda10cc0269f15e29ecdb7c9fdbe7d8eae
  archive size:      852,853 bytes

The distributed GPL version 2 text is in license.txt.

What is kept
------------

Only the 81 C units the final garbage-collected Desktop link
actually uses: the AES bindings ppdg000, 001, 002, 005, 008, 011,
014-020, 026-029, 031-036, 039, 041, 043-046, 051-060, 062, 066,
067, 074-076, 079, 080, 089, 091-093, 097, and ppdgem; the VDI
bindings ppdv030, 031, 045, 047, 061, 064-066, 071-074, 085, 090,
092, 093, 101, 102, 105, 106, 113, 121, 122, and ppdvdi; plus
rc_copy, rc_equal, rc_inter, and x_mul_di.  All .c files.

SHA-256 of the C-locale, filename-sorted manifest of retained C file
hashes and basenames:

  e017d5ba77c1c607a72d3ebde8f380f659f33f5da7d4d5c3913b3f47b4afab3e

Same for the four headers:

  edae37db73f1600aad52c8d2ba0fbc3544f666dbacf008e5850fb69077dd8f8d

The ELKS seams
--------------

  - ppdgem.c and ppdvdi.c issue INT EFh directly with the original
    8086 register and array conventions; no DOS wrapper is linked.
  - Pointer slots are explicit packed offset/segment word pairs.
    Near pointers go out as DS:offset, null is 0:0, and a foreign
    returned segment is rejected, not truncated.
  - x_mul_di.c does its multiply and divide with 16-bit shift/add
    and restoring division.  Signed results truncate toward zero,
    divide by zero returns zero, overflow pins to -32768 or 32767.
  - The multi-application process bindings keep the original
    word-pair ABI and are only built for the trap-owned profile.
  - The VDI wrappers reuse the original Desktop arrays
    (USER_INTIN=1) so there is one owner and no extra near data.

No conversion record, RPC layer, 32-bit C arithmetic, floating
point, or dynamic wrapper anywhere.
