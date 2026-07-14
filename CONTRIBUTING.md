# Contributing

Contributions must preserve the IBM PC/XT target and the historical GEM
notices.

- Generate only 8088/8086 instructions and real-mode addressing.
- Use only 8-bit and 16-bit target-side scalar arithmetic.
- Represent unavoidable wider ABI fields as explicit word pairs with
  documented carry, rounding, saturation, and overflow behavior.
- Do not introduce floating point, 32-bit C arithmetic, dynamic compatibility
  layers, resource conversion, or DOS command-shell paths.
- Keep graphics and input ownership behind the ELKS GEM kernel boundary.
- Prefer near data and bounded fixed storage in hot paths.
- Preserve original GEM control flow where an ELKS-specific seam is not
  required.

Run the complete gate before submitting a change:

```sh
make -f Makefile.elks ELKS_ROOT=/path/to/elks clean audit
```

Submitted changes must be licensed under GNU GPL version 2 only and must not
remove or replace applicable historical copyright and license notices.
