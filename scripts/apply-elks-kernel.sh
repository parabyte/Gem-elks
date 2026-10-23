#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: sh scripts/apply-elks-kernel.sh /path/to/elks" >&2
	exit 2
fi

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
elks_dir=$1
patch_file=$project_dir/patches/elks/0001-gemtrap-kernel-broker.patch

if [ ! -d "$elks_dir/.git" ] || [ ! -f "$elks_dir/elks/config.in" ]; then
	echo "not an ELKS source tree: $elks_dir" >&2
	exit 2
fi

if git -C "$elks_dir" apply --reverse --check "$patch_file" >/dev/null 2>&1; then
	echo "ELKS GEM trap broker is already applied."
	exit 0
fi

git -C "$elks_dir" apply --check "$patch_file"
git -C "$elks_dir" apply "$patch_file"

echo "Applied the ELKS GEM trap broker."
echo "Enable CONFIG_GEM_TRAP, then rebuild the ELKS kernel and libc."
