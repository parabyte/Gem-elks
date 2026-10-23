Original GEM Pacific C bindings
===============================

These files came from the OpenGEM 7 RC1 SDK at revision
ac06b1a3fec3f3e8defcaaf7ea0338c38c3cef46.

Source archive:

  OpenGEM-7-RC1-SDK.zip
  SHA-256 9bec979807f1f3247e56647e50d068f39f6670900129b8ae82874924daf6023d

The GPL version 2 text is in license.txt.

ELKS changes
------------

  - ppdgem.c and ppdvdi.c send requests through the local AF_UNIX socket.
  - Pointer slots are packed offset and segment word pairs.
  - x_mul_di.c uses 16-bit multiply and divide.
  - VDI calls share Desktop's original arrays.
  - GEM process and memory bindings are not included.  ELKS owns them.
