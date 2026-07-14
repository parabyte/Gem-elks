#!/bin/sh
#
# Reject instructions outside the IBM 8088/8086 contract, arithmetic which
# is prohibitively expensive in this runtime, and any x87 dependency.
#
# ia16 ELF objects carry an i386 BFD container tag, so objdump must receive
# -mi8086 explicitly.  Without it the same bytes are decoded with 32-bit
# registers and the audit itself becomes meaningless.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if [ "$#" -eq 0 ]; then
	echo "usage: $0 IA16-OBJECT..." >&2
	exit 2
fi

objdump=${OBJDUMP:-"$ROOT_DIR/vendor/elks/cross/bin/ia16-elf-objdump"}

if [ ! -x "$objdump" ]; then
	echo "8086 codegen gate: objdump not executable: $objdump" >&2
	exit 2
fi

temporary=${TMPDIR:-/tmp}/elks-gem-codegen.$$
trap 'rm -f "$temporary"' 0 HUP INT TERM

"$objdump" -dr -mi8086 "$@" > "$temporary"

# Match only decoded instruction lines.  The byte column may contain from
# one through several hexadecimal bytes before objdump prints the mnemonic.
# MUL and DIV are legal 8086 opcodes but intentionally forbidden here: all
# runtime scaling and wrapping must use pointer increments, masks, or bounded
# shift/add/subtract sequences whose cost is predictable on a 4.77 MHz 8088.
tab=$(printf '\t')
instruction="^[[:space:]]*[0-9a-f]+:${tab}[0-9a-f ]+${tab}"
forbidden='(mulw?|imulw?|divw?|idivw?|pusha|pushaw|popa|popaw|enter|leave|bound|insb|insw|outsb|outsw|arpl|lar|lsl|clts|lmsw|smsw|verr|verw|data32|addr32|f[a-z0-9]+)'

if grep -E -i "$instruction$forbidden([[:space:]]|$)" "$temporary"; then
	echo "8086 codegen gate: forbidden instruction found" >&2
	exit 1
fi

# Immediate pushes and multi-bit immediate shifts were added by the 80186.
# The 8086 spelling of a one-bit shift has no immediate operand, while a
# variable count must pass through CL.
if grep -E -i "$instruction(push|sal|sar|shl|shr|rol|ror|rcl|rcr)[[:space:]]+\\\$" \
	"$temporary"; then
	echo "8086 codegen gate: 80186 immediate operand found" >&2
	exit 1
fi

# A decoded 32-bit register is also rejected independently of the mnemonic,
# making an accidental operand-size prefix visible even if binutils changes
# how it prints that instruction in a later release.
if grep -E -i "$instruction.*%(e(ax|bx|cx|dx|si|di|bp|sp)|[cdefgs]r[0-9]+)" \
	"$temporary"; then
	echo "8086 codegen gate: 32-bit register found" >&2
	exit 1
fi

echo "8086 codegen gate: PASS objects=$#"
