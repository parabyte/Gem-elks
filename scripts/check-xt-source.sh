#!/bin/sh
#
# Reject source constructs which can introduce arithmetic wider than one
# 8088/8086 word or a floating-point dependency.
#
# Comments and quoted strings are removed before matching, so beginner-facing
# documentation may still explain why a forbidden C type is not used.  This
# is a source-level companion to check-8086-codegen.sh: the disassembly gate
# proves the emitted instructions, while this gate also rejects four-byte C
# scalar containers which might otherwise compile into innocent-looking copy
# instructions.

set -eu

if [ "$#" -eq 0 ]; then
	echo "XT source gate: no source files supplied" >&2
	exit 1
fi

temporary=${TMPDIR:-/tmp}/elks-gem-source.$$
trap 'rm -f "$temporary"' EXIT HUP INT TERM

#
# Keep line numbers while removing C block comments, C++ line comments, and
# quoted strings/characters.  The scanner deliberately leaves preprocessor
# directives visible so forbidden system headers cannot be added silently.
#
awk '
BEGIN {
	in_comment = 0
}
{
	line = $0
	clean = ""
	i = 1
	while (i <= length(line)) {
		ch = substr(line, i, 1)
		next_ch = substr(line, i + 1, 1)
		if (in_comment) {
			if (ch == "*" && next_ch == "/") {
				in_comment = 0
				i += 2
			} else {
				i++
			}
			continue
		}
		if (ch == "/" && next_ch == "*") {
			in_comment = 1
			clean = clean " "
			i += 2
			continue
		}
		if (ch == "/" && next_ch == "/")
			break
		if (ch == "\"" || ch == "\047") {
			quote = ch
			clean = clean " "
			i++
			while (i <= length(line)) {
				ch = substr(line, i, 1)
				if (ch == "\\") {
					i += 2
					continue
				}
				i++
				if (ch == quote)
					break
			}
			continue
		}
		clean = clean ch
		i++
	}
	print FILENAME ":" FNR ":" clean
}
' "$@" > "$temporary"

types='(^|[^[:alnum:]_])(long|float|double|LONG|ULONG|int32_t|uint32_t|int64_t|uint64_t|int_least32_t|uint_least32_t|int_fast32_t|uint_fast32_t|int_least64_t|uint_least64_t|int_fast64_t|uint_fast64_t)([^[:alnum:]_]|$)'
wide_constants='(^|[^[:alnum:]_])(0[xX][0-9A-Fa-f]+|[0-9]+)[uU]?[lL]([^[:alnum:]_]|$)'
float_constants='(^|[^[:alnum:]_])(([0-9]+\.[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?|[0-9]+[eE][+-]?[0-9]+)[fFlL]?([^[:alnum:]_]|$)'
float_headers='#[[:space:]]*include[[:space:]]*[<"](math|float)\.h[>"]'

failed=0
for pattern in "$types" "$wide_constants" "$float_constants" "$float_headers"; do
	if grep -En "$pattern" "$temporary"; then
		failed=1
	fi
done

if [ "$failed" -ne 0 ]; then
	echo "XT source gate: forbidden wide or floating-point construct found" >&2
	exit 1
fi

echo "XT source gate: PASS files=$#"
