# Original GEM lineage measurement

The release reports two source-lineage measurements for the linked Desktop
client.

- Core lineage: 9,307 GEM/direct-derived SLOC divided by 10,188 core SLOC,
  or 91.35%.
- Complete-client lineage: 9,307 GEM/direct-derived SLOC divided by 11,241
  retained SLOC, or 82.80%.

The complete-client denominator includes every required ELKS/POSIX boundary.
The core denominator excludes only ELKS-classified functions in these three
explicit modules:

```text
src/aes/gemdos_posix.c
src/aes/gem_wordpair.c
src/aes/opengem/gemdesk_posix.c
```

Direct-derived GEM functions in those files remain classified as GEM.  The
audit cannot raise the percentage by including dormant imports: it reads the
final linker map, counts only retained function sections, requires exact
source/function pairs in the pinned manifest, and checks for duplicate strong
project symbols.

Reproduce the measurement with:

```sh
make -f Makefile.elks ELKS_ROOT=/path/to/elks lineage-audit
```

The runtime asset lineage is independent: all four installed Desktop assets
are original byte-for-byte files and are verified by `asset-audit`.
