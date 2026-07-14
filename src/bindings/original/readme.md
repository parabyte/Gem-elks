# Original GEM Pacific C bindings with ELKS seams

This directory is the final linked binding closure for the native OpenGEM
Desktop client.  It began as a normalized import of the original GEM AES and
VDI Pacific C bindings and retains their function-level trap flow.

## Upstream provenance

The primary binding source came from the OpenGEM 7 RC1 SDK at revision
`ac06b1a3fec3f3e8defcaaf7ea0338c38c3cef46`:

```text
source/OpenGEM-7-RC1-SDK.zip
OpenGEM-7-SDK/PROGRAMMING BINDINGS AND COMPILERS/
GEM Developers Kit for Pacific C (Pacific C bindings)/LIBSRC
```

- archive Git blob: `fc43118a005e08c7cbe5c2c7c7cb6f421755326b`
- archive SHA-256:
  `9bec979807f1f3247e56647e50d068f39f6670900129b8ae82874924daf6023d`
- archive size: 19,006,381 bytes

The multi-application entries came from the separately pinned SeaSIP Pacific C
GEM Developers Kit archive:

- archive: `ppd_gdk.zip`
- archive SHA-256:
  `465342231610a0244f585f0107ee56dda10cc0269f15e29ecdb7c9fdbe7d8eae`
- archive size: 852,853 bytes

The distributed GPL version 2 text remains in `license.txt`.

## Retained closure

Only the 81 C units selected by the final garbage-collected Desktop link are
present.  They comprise the AES bindings `ppdg000`, `001`, `002`, `005`,
`008`, `011`, `014` through `020`, `026` through `029`, `031` through `036`,
`039`, `041`, `043` through `046`, `051` through `060`, `062`, `066`, `067`,
`074` through `076`, `079`, `080`, `089`, `091` through `093`, `097`, and
`ppdgem`; the VDI bindings `ppdv030`, `031`, `045`, `047`, `061`, `064`
through `066`, `071` through `074`, `085`, `090`, `092`, `093`, `101`, `102`,
`105`, `106`, `113`, `121`, `122`, and `ppdvdi`; plus `rc_copy`, `rc_equal`,
`rc_inter`, and `x_mul_di`.  Every name above has a `.c` suffix.

The SHA-256 of a C-locale, filename-sorted manifest containing each retained C
file hash and basename is:

```text
e017d5ba77c1c607a72d3ebde8f380f659f33f5da7d4d5c3913b3f47b4afab3e
```

The equivalent four-header manifest hash is:

```text
edae37db73f1600aad52c8d2ba0fbc3544f666dbacf008e5850fb69077dd8f8d
```

## Narrow ELKS adaptations

- `ppdgem.c` and `ppdvdi.c` issue `INT EFh` directly with original 8086
  register and array conventions; no DOS interrupt wrapper is linked.
- Pointer slots are explicit packed offset/segment word pairs.  Near pointers
  are exported as DS:offset, null is 0:0, and a foreign returned segment is
  rejected rather than truncated.
- `x_mul_di.c` uses 16-bit-half shift/add and restoring division.  Signed
  results truncate toward zero, division by zero returns zero, and overflow
  saturates to -32768 or 32767.
- Multi-application process bindings use the original word-pair ABI and are
  enabled only for the trap-owned build profile.
- VDI wrappers reuse the original Desktop arrays with `USER_INTIN=1`, avoiding
  duplicate array owners and extra near-data storage.

No conversion record, RPC layer, 32-bit C arithmetic, floating point, or
dynamic wrapper is introduced.
