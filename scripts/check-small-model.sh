#!/bin/sh
#
# Reject an ELKS executable which cannot fit its ia16 separate-I/D model.
# objdump86 reports the near text, data, and bss sizes from the final ELKS
# a.out header.  A medium-model file has a 64-byte header and stores its far
# text size as explicit low/high 16-bit words at byte offset 48.  Reading the
# halves separately keeps this audit aligned with the target rule: each real
# mode code segment must fit in one 16-bit offset space.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if [ "$#" -ne 1 ]; then
	echo "usage: $0 ELKS-AOUT" >&2
	exit 2
fi

binary=$1
objdump86=${OBJDUMP86:-"$ROOT_DIR/vendor/elks/elks/tools/bin/objdump86"}

if [ ! -x "$objdump86" ]; then
	echo "small-model gate: objdump86 not executable: $objdump86" >&2
	exit 2
fi

sizes=$(
	"$objdump86" -h "$binary" |
	awk '$1 == "text" && $2 == "data" && $3 == "bss" {
		getline
		print $1, $2, $3
		exit
	}'
)

set -- $sizes
if [ "$#" -ne 3 ]; then
	echo "small-model gate: cannot read ELKS a.out sizes: $binary" >&2
	exit 1
fi

text_size=$1
data_size=$2
bss_size=$3
data_total=$((data_size + bss_size))

header_length=$(od -An -tu1 -j4 -N1 "$binary" | tr -d ' ')

#
# execve appends argc, argv, and environment strings to the data segment and
# then rounds the allocation up to one 16-byte paragraph.  Reserve a bounded
# deployment allowance so a link which fits only with an empty environment is
# rejected here rather than later by the ELKS loader.  This is host-only audit
# arithmetic; it introduces no wide target operation.  Callers staging an
# unusually large environment may raise the allowance explicitly.
#
exec_headroom=${ELKS_EXEC_HEADROOM:-1024}
case $exec_headroom in
	''|*[!0-9]*)
		echo "ELKS model gate: invalid ELKS_EXEC_HEADROOM: $exec_headroom" >&2
		exit 2
		;;
esac

#
# ia16-elf encodes the requested heap and stack as the low and high 16-bit
# halves of a_total at header byte 24.  Both values are unscaled bytes.  ELKS
# places data, BSS, heap, and stack in the same 64 KiB data segment, so checking
# data+BSS alone can accept an image which the kernel cannot actually lay out.
# Host-shell arithmetic is used only by this build audit; no wide operation is
# introduced into the 8086 executable.
#
set -- $(od -An -tu2 -j24 -N4 "$binary")
if [ "$#" -ne 2 ]; then
	echo "ELKS model gate: cannot read heap/stack words: $binary" >&2
	exit 1
fi
header_heap=$1
header_stack=$2

set -- $(od -An -tu2 -j6 -N2 "$binary")
if [ "$#" -ne 1 ]; then
	echo "ELKS model gate: cannot read a.out version: $binary" >&2
	exit 1
fi
aout_version=$1

#
# ELKS a.out version 1 interprets zero chmem/minstack words as 4096-byte
# defaults.  Version 0 instead treats a nonzero chmem word as the complete
# data-segment allocation.  Mirror those loader rules exactly enough to avoid
# reporting a zero heap which the kernel will silently expand to 4 KiB.
#
case $aout_version in
1)
	stack_size=$header_stack
	[ "$stack_size" -ne 0 ] || stack_size=4096
	if [ "$header_heap" -ge 65520 ]; then
		#
		# 0xfff0..0xffff is ELKS's maximum-heap request, not a
		# literal heap byte count.  The loader grows the complete data
		# segment to 0xfff0 when data+BSS+stack+argv is smaller.
		#
		heap_size=max
		resident_data_total=$((data_total + stack_size + exec_headroom))
		if [ "$resident_data_total" -lt 65520 ]; then
			resident_data_total=65520
		fi
		loader_data_total=$((resident_data_total + 15))
	else
		heap_size=$header_heap
		[ "$heap_size" -ne 0 ] || heap_size=4096
		resident_data_total=$((data_total + heap_size + stack_size))
		loader_data_total=$((resident_data_total + exec_headroom + 15))
	fi
	;;
0)
	if [ "$header_heap" -eq 0 ]; then
		heap_size=4096
		stack_size=4096
		resident_data_total=$((data_total + heap_size + stack_size))
		loader_data_total=$((resident_data_total + exec_headroom + 15))
	else
		heap_size=0
		stack_size=0
		resident_data_total=$header_heap
		loader_data_total=$((resident_data_total + 15))
		if [ "$resident_data_total" -lt $((data_total + exec_headroom)) ]; then
			echo "ELKS model gate: v0 data total leaves less than $exec_headroom bytes for argv/environment: $binary" >&2
			exit 1
		fi
	fi
	;;
*)
	echo "ELKS model gate: unsupported a.out version $aout_version: $binary" >&2
	exit 1
	;;
esac

far_text_size=0
if [ "$header_length" -ge 56 ]; then
	set -- $(od -An -tu2 -j48 -N4 "$binary")
	if [ "$#" -ne 2 ]; then
		echo "ELKS model gate: cannot read far-text words: $binary" >&2
		exit 1
	fi
	far_text_size=$1
	far_text_high=$2
	if [ "$far_text_high" -ne 0 ]; then
		echo "ELKS model gate: far text exceeds one 16-bit segment: $binary" >&2
		exit 1
	fi
fi

if [ "$text_size" -ge 65536 ]; then
	echo "ELKS model gate: near text $text_size exceeds 65535: $binary" >&2
	exit 1
fi

if [ "$far_text_size" -ge 65536 ]; then
	echo "ELKS model gate: far text $far_text_size exceeds 65535: $binary" >&2
	exit 1
fi

if [ "$data_total" -ge 65536 ]; then
	echo "ELKS model gate: data+bss $data_total exceeds 65535: $binary" >&2
	exit 1
fi

if [ "$loader_data_total" -ge 65536 ]; then
	echo "ELKS model gate: loader data $loader_data_total including argv/environment and paragraph headroom exceeds 65535: $binary" >&2
	exit 1
fi

echo "ELKS model gate: PASS near_text=$text_size far_text=$far_text_size data+bss=$data_total heap=$heap_size stack=$stack_size resident_data=$resident_data_total exec_headroom=$exec_headroom loader_data=$loader_data_total $binary"
