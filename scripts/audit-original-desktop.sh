#!/bin/sh

# Audit only implementation which survived the gemdesk-trap link.
#
# The linker map is the source of truth.  In particular, merely placing an
# original source file in the repository or an object in libgemtrap.a does
# not give it any credit.  Only non-empty function sections in the final
# memory map are counted.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ELKS_ROOT=${ELKS_ROOT:-"$ROOT_DIR/vendor/elks"}

if test "$#" -ne 3; then
	printf 'usage: %s LINKER_MAP UPSTREAM_FUNCTION_MANIFEST TARGET_BOUNDARY_MANIFEST\n' \
		"$0" >&2
	exit 2
fi

map_file=$1
manifest_file=$2
boundary_file=$3

test -r "$map_file" || {
	printf '%s: cannot read linker map: %s\n' "$0" "$map_file" >&2
	exit 2
}
test -r "$manifest_file" || {
	printf '%s: cannot read provenance manifest: %s\n' \
		"$0" "$manifest_file" >&2
	exit 2
}
test -r "$boundary_file" || {
	printf '%s: cannot read target-boundary manifest: %s\n' \
		"$0" "$boundary_file" >&2
	exit 2
}

: "${CC:=$ELKS_ROOT/cross/bin/ia16-elf-gcc}"
: "${NM:=$ELKS_ROOT/cross/bin/ia16-elf-nm}"
: "${CPPFLAGS:=-DELKS=1 -DUNIX=1 -DI8086=1 -DNDEBUG=1 -DMULTIAPP=1 -DSMALL_DATA=1 -I src/include -I $ELKS_ROOT/libc/include -I $ELKS_ROOT/elks/include}"
: "${BINDINGS_CPPFLAGS:=-I src/bindings/original -DGEM_BINDINGS_ENABLE_DOS_PROCESS=1 -DUSER_INTIN=1}"
export CC NM CPPFLAGS BINDINGS_CPPFLAGS

tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/gem-lineage.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

# ia16-gcc puts the callable body in a section ending in '$'.  Its matching
# '!' and '&' sections are zero-sized medium-model metadata/thunks.  Start at
# "Memory map" so discarded input sections cannot enter the measurement.
awk '
	/^Memory map$/ {
		in_map = 1
		next
	}
	!in_map {
		next
	}
	/^ \.fartext\.s\./ && /\$$/ {
		function_name = $1
		sub(/^\.fartext\.s\./, "", function_name)
		sub(/\.[0-7][0-7][0-7][0-7][0-7]\$$/, "", function_name)
		while (sub(/\.(constprop|isra|part)\.[0-9]+$/, "",
			   function_name))
			;
		pending = function_name
		next
	}
	pending != "" && /\/obj\/.*\.o$/ {
		source_name = $NF
		sub(/^.*\/obj\//, "", source_name)
		# The original-client far-data ABI uses an isolated object tree.
		sub(/^trap\//, "", source_name)
		sub(/\.o$/, ".c", source_name)
		print pending "\t" "src/" source_name
		pending = ""
		next
	}
	pending != "" && /libgemtrap\.a\([^)]*\.o\)$/ {
		source_name = $NF
		sub(/^.*\(/, "", source_name)
		sub(/\.o\)$/, ".c", source_name)
		print pending "\t" "src/bindings/original/" source_name
		pending = ""
		next
	}
	pending != "" && NF {
		pending = ""
	}
' "$map_file" | LC_ALL=C sort -u > "$tmp_dir/functions"

test -s "$tmp_dir/functions" || {
	printf '%s: no retained project function sections found in %s\n' \
		"$0" "$map_file" >&2
	exit 1
}

# Obtain the exact project object set which reached the final memory map.
# Archive members are retained separately because nm names them as
# "archive:member" records.
awk '
	/^Memory map$/ { in_map = 1; next }
	in_map {
		for (i = 1; i <= NF; i++)
			if ($i ~ /\/obj\/.*\.o$/)
				print $i
	}
' "$map_file" | LC_ALL=C sort -u > "$tmp_dir/objects"

awk '
	match($0, /[^[:space:]]*libgemtrap\.a\([^)]*\.o\)/) {
		entry = substr($0, RSTART, RLENGTH)
		sub(/^.*\(/, "", entry)
		sub(/\)$/, "", entry)
		print entry
	}
' "$map_file" | LC_ALL=C sort -u > "$tmp_dir/members"

archive_file=$(awk '
	match($0, /[^[:space:]]*libgemtrap\.a\([^)]*\.o\)/) {
		entry = substr($0, RSTART, RLENGTH)
		sub(/\([^)]*\)$/, "", entry)
		print entry
		exit
	}
' "$map_file")

: > "$tmp_dir/nm"
while IFS= read -r object_file; do
	test -r "$object_file" || {
		printf '%s: linked object vanished: %s\n' "$0" "$object_file" >&2
		exit 1
	}
	"$NM" -A -g --defined-only "$object_file" >> "$tmp_dir/nm"
done < "$tmp_dir/objects"

if test -n "$archive_file"; then
	test -r "$archive_file" || {
		printf '%s: linked archive vanished: %s\n' "$0" "$archive_file" >&2
		exit 1
	}
	"$NM" -A -g --defined-only "$archive_file" >> "$tmp_dir/nm"
fi

perl - "$tmp_dir/functions" "$manifest_file" "$boundary_file" \
	"$tmp_dir/nm" "$tmp_dir/members" <<'PERL'
use strict;
use warnings;
use Cwd qw(abs_path);
use Digest::SHA qw(sha256_hex);
use Text::ParseWords qw(shellwords);

my ($function_file, $manifest_file, $boundary_file, $nm_file,
	$member_file) = @ARGV;
my (%wanted, %upstream, %boundary_source, %loaded_member);
my %source_name_alias = (
	"src/aes/opengem/deskapp1.c\0gemdesk_strlcpy" => 'strlcpy',
	"src/aes/opengem/deskapp1.c\0gemdesk_strlcat" => 'strlcat',
);

sub read_pairs
{
	my ($path, $target, $with_class) = @_;
	open my $fh, '<', $path or die "$path: $!\n";
	while (my $line = <$fh>) {
		$line =~ s/\r?\n$//;
		next if $line =~ /^\s*(?:#|$)/;
		my @field = split /\s+/, $line;
		if ($with_class) {
			die "$path: malformed manifest line: $line\n"
				unless @field == 3 && $field[0] eq 'gem';
			$target->{lc($field[1]) . "\0" . lc($field[2])} = 1;
		} else {
			die "$path: malformed function line: $line\n"
				unless @field == 2;
			$target->{$field[1]}{$field[0]} = 1;
		}
	}
	close $fh;
}

read_pairs($function_file, \%wanted, 0);
read_pairs($manifest_file, \%upstream, 1);

open my $boundary_fh, '<', $boundary_file
	or die "$boundary_file: $!\n";
while (my $line = <$boundary_fh>) {
	$line =~ s/\r?\n$//;
	next if $line =~ /^\s*(?:#|$)/;
	my @field = split /\s+/, $line;
	die "$boundary_file: malformed boundary line: $line\n"
		unless @field == 2 && $field[0] eq 'boundary';
	die "$boundary_file: duplicate boundary source: $field[1]\n"
		if $boundary_source{lc($field[1])}++;
}
close $boundary_fh;

open my $members, '<', $member_file or die "$member_file: $!\n";
while (my $member = <$members>) {
	chomp $member;
	$loaded_member{$member} = 1 if length $member;
}
close $members;

# Return two same-length strings.  The first has only comments blanked and is
# used for physical SLOC.  The second also blanks string/character literals,
# which makes brace and parenthesis matching deterministic.
sub mask_c_text
{
	my ($input) = @_;
	my ($count, $parse, $state, $escape) = ('', '', 'normal', 0);
	my $length = length $input;

	for (my $i = 0; $i < $length; ++$i) {
		my $ch = substr($input, $i, 1);
		my $next = $i + 1 < $length ? substr($input, $i + 1, 1) : '';

		if ($state eq 'block') {
			if ($ch eq '*' && $next eq '/') {
				$count .= '  ';
				$parse .= '  ';
				++$i;
				$state = 'normal';
			} elsif ($ch eq "\n") {
				$count .= "\n";
				$parse .= "\n";
			} else {
				$count .= ' ';
				$parse .= ' ';
			}
			next;
		}

		if ($state eq 'line') {
			if ($ch eq "\n") {
				$count .= "\n";
				$parse .= "\n";
				$state = 'normal';
			} else {
				$count .= ' ';
				$parse .= ' ';
			}
			next;
		}

		if ($state eq 'string' || $state eq 'char') {
			$count .= $ch;
			$parse .= $ch eq "\n" ? "\n" : ' ';
			if ($escape) {
				$escape = 0;
			} elsif ($ch eq '\\') {
				$escape = 1;
			} elsif (($state eq 'string' && $ch eq '"')
				 || ($state eq 'char' && $ch eq "'")) {
				$state = 'normal';
			}
			next;
		}

		if ($ch eq '/' && $next eq '*') {
			$count .= '  ';
			$parse .= '  ';
			++$i;
			$state = 'block';
		} elsif ($ch eq '/' && $next eq '/') {
			$count .= '  ';
			$parse .= '  ';
			++$i;
			$state = 'line';
		} elsif ($ch eq '"') {
			$count .= $ch;
			$parse .= ' ';
			$state = 'string';
		} elsif ($ch eq "'") {
			$count .= $ch;
			$parse .= ' ';
			$state = 'char';
		} else {
			$count .= $ch;
			$parse .= $ch;
		}
	}

	die "unterminated block comment\n" if $state eq 'block';
	return ($count, $parse);
}

sub active_source_lines
{
	my ($source) = @_;
	my @command = (shellwords($ENV{CC}), '-E',
		shellwords($ENV{CPPFLAGS}));
	push @command, shellwords($ENV{BINDINGS_CPPFLAGS})
		if $source =~ m{^src/bindings/original/};
	push @command, $source;

	open my $cpp, '-|', @command
		or die "cannot run preprocessor for $source: $!\n";
	my $source_abs = abs_path($source);
	my ($current, $line_number) = (0, 1);
	my %active;

	while (my $line = <$cpp>) {
		if ($line =~ /^#\s+(\d+)\s+"([^"]+)"/) {
			$line_number = $1;
			my $marker = $2;
			my $marker_abs = $marker =~ /^</ ? undef : abs_path($marker);
			$current = defined($marker_abs) && defined($source_abs)
				&& $marker_abs eq $source_abs;
			next;
		}
		if ($current) {
			$active{$line_number} = 1 if $line =~ /\S/;
			++$line_number;
		}
	}
	close $cpp or die "preprocessor failed for $source\n";
	return \%active;
}

sub line_number_at
{
	my ($text, $position) = @_;
	return 1 + (substr($text, 0, $position) =~ tr/\n//);
}

sub locate_function
{
	my ($source, $name, $count_text, $parse_text, $depth) = @_;
	pos($parse_text) = 0;

	while ($parse_text =~ /(?<![A-Za-z0-9_])\Q$name\E[ \t\r\n]*\(/g) {
		my $name_position = $-[0];
		next unless $depth->[$name_position] == 0;
		my $open_paren = $+[0] - 1;
		my $paren_depth = 1;
		my $position = $open_paren + 1;

		for (; $position < length($parse_text); ++$position) {
			my $ch = substr($parse_text, $position, 1);
			++$paren_depth if $ch eq '(';
			--$paren_depth if $ch eq ')';
			last if $paren_depth == 0;
		}
		next if $paren_depth != 0;

		++$position;
		++$position while $position < length($parse_text)
			&& substr($parse_text, $position, 1) =~ /\s/;
		next if $position >= length($parse_text);
		my $after = substr($parse_text, $position, 1);
		next if $after eq ';' || $after eq ',' || $after eq '=';

		my $open_brace;
		for (my $scan = $position; $scan < length($parse_text); ++$scan) {
			my $ch = substr($parse_text, $scan, 1);
			if ($ch eq '{' && $depth->[$scan] == 0) {
				$open_brace = $scan;
				last;
			}
			last if $ch eq '}' && $depth->[$scan] == 0;
		}
		next unless defined $open_brace;

		my $close_brace;
		for (my $scan = $open_brace + 1;
		     $scan < length($parse_text); ++$scan) {
			if (substr($parse_text, $scan, 1) eq '}'
			    && $depth->[$scan] == 1) {
				$close_brace = $scan;
				last;
			}
		}
		next unless defined $close_brace;

		my $first_line = line_number_at($parse_text, $name_position);
		my $last_line = line_number_at($parse_text, $close_brace);
		my @lines = split /\n/, $count_text, -1;

		# Pacific C commonly puts WORD or VOID on the line immediately before
		# the function name.  Include contiguous declaration-specifier lines,
		# but never pull a preceding statement or function into this body.
		while ($first_line > 1) {
			my $previous = $lines[$first_line - 2];
			last if $previous !~ /\S/;
			last if $previous =~ /[;{}]\s*$/;
			last if $previous =~ /^\s*#/;
			--$first_line;
		}
		return ($first_line, $last_line);
	}

	die "$source: retained function '$name' was not found in active source\n";
}

my (%line_class, %line_boundary, %function_class, %function_sloc,
	%function_count, %boundary_source_seen, %source_seen);
my $closure_material = '';

for my $source (sort keys %wanted) {
	open my $source_fh, '<', $source or die "$source: $!\n";
	my $raw;
	{
		local $/;
		$raw = <$source_fh>;
	}
	close $source_fh;
	$source_seen{$source} = 1;
	$closure_material .= $source . "\0" . sha256_hex($raw) . "\n";

	my $active = active_source_lines($source);
	my @raw_lines = split /(?<=\n)/, $raw, -1;
	for (my $index = 0; $index < @raw_lines; ++$index) {
		$raw_lines[$index] = "\n" unless $active->{$index + 1};
	}
	my $active_raw = join '', @raw_lines;
	my ($count_text, $parse_text) = mask_c_text($active_raw);

	my @depth;
	my $brace_depth = 0;
	for (my $position = 0; $position < length($parse_text); ++$position) {
		$depth[$position] = $brace_depth;
		my $ch = substr($parse_text, $position, 1);
		++$brace_depth if $ch eq '{';
		--$brace_depth if $ch eq '}';
		die "$source: unmatched closing brace\n" if $brace_depth < 0;
	}
	die "$source: unmatched opening brace\n" if $brace_depth != 0;

	my @count_lines = split /\n/, $count_text, -1;
	for my $name (sort keys %{ $wanted{$source} }) {
		my $source_name = $source_name_alias{$source . "\0" . $name}
			|| $name;
		my ($first, $last) = locate_function($source, $source_name,
			$count_text, $parse_text, \@depth);
		my $class = $upstream{lc($source) . "\0" . lc($source_name)}
			? 'gem' : 'elks';
		my $is_boundary = $class eq 'elks'
			&& $boundary_source{lc($source)};
		$boundary_source_seen{lc($source)} = 1 if $is_boundary;
		++$function_count{$class};
		my $function_key = $source . "\0" . $name;
		$function_class{$function_key} = $class;
		for my $line ($first .. $last) {
			next unless $count_lines[$line - 1] =~ /\S/;
			my $key = $source . "\0" . $line;
			if (exists $line_class{$key} && $line_class{$key} ne $class) {
				die "$source:$line: overlapping lineage classes\n";
			}
			$line_class{$key} = $class;
			$line_boundary{$key} = 1 if $is_boundary;
			++$function_sloc{$function_key};
		}
	}
}

my %sloc = (gem => 0, elks => 0);
++ $sloc{$_} for values %line_class;
my $boundary_sloc = scalar keys %line_boundary;
for my $source (sort keys %boundary_source) {
	die "$boundary_file: inactive or non-ELKS boundary source: $source\n"
		unless $boundary_source_seen{$source};
}
my %elks_source_sloc;
for my $key (keys %line_class) {
	next unless $line_class{$key} eq 'elks';
	my ($source) = split /\0/, $key, 2;
	++$elks_source_sloc{$source};
}
my $total = $sloc{gem} + $sloc{elks};
die "zero implementation SLOC\n" unless $total;
my $hundredths = int(($sloc{gem} * 10000 + int($total / 2)) / $total);
my $whole = int($hundredths / 100);
my $fraction = $hundredths - $whole * 100;
my $core_elks = $sloc{elks} - $boundary_sloc;
my $core_total = $sloc{gem} + $core_elks;
die "zero core implementation SLOC\n" unless $core_total;
my $core_hundredths = int(($sloc{gem} * 10000 + int($core_total / 2))
	/ $core_total);
my $core_whole = int($core_hundredths / 100);
my $core_fraction = $core_hundredths - $core_whole * 100;

# Audit strong globals across only the direct objects and archive members
# selected by this link.  ia16 companion symbols ending in ! or & are not
# independent definitions.  Common and weak definitions are intentionally
# not strong-symbol failures.
my %definition;
open my $nm, '<', $nm_file or die "$nm_file: $!\n";
while (my $line = <$nm>) {
	chomp $line;
	next unless $line =~ /^(.*):([0-9A-Fa-f]+)\s+([A-Za-z?])\s+(\S+)$/;
	my ($owner, $type, $symbol) = ($1, $3, $4);
	if ($owner =~ /libgemtrap\.a:([^:]+)$/) {
		next unless $loaded_member{$1};
	}
	next unless $type =~ /^[A-Z]$/;
	next if $type =~ /^[CUVW]$/;
	next if $symbol =~ /[!&]$/;
	$definition{$symbol}{$owner} = 1;
}
close $nm;

my @duplicate;
for my $symbol (sort keys %definition) {
	my @owner = sort keys %{ $definition{$symbol} };
	push @duplicate, "$symbol: " . join(', ', @owner) if @owner > 1;
}
if (@duplicate) {
	print STDERR "duplicate strong project symbols:\n";
	print STDERR "  $_\n" for @duplicate;
	exit 1;
}

my $function_total = ($function_count{gem} || 0)
	+ ($function_count{elks} || 0);
printf "original Desktop retained-function audit: PASS\n";
printf "  GEM original/direct-derived implementation SLOC: %u\n", $sloc{gem};
printf "  ELKS/XT-only implementation SLOC:              %u\n", $sloc{elks};
printf "  total retained implementation SLOC:            %u\n", $total;
printf "  GEM lineage:                                    %u/%u = %u.%02u%%\n",
	$sloc{gem}, $total, $whole, $fraction;
printf "  explicit ELKS/POSIX boundary SLOC:              %u\n",
	$boundary_sloc;
printf "  core ELKS/XT adaptation SLOC:                   %u\n", $core_elks;
printf "  core GEM lineage excluding target boundary:     %u/%u = %u.%02u%%\n",
	$sloc{gem}, $core_total, $core_whole, $core_fraction;
printf "  retained functions:                             %u GEM + %u ELKS = %u\n",
	($function_count{gem} || 0), ($function_count{elks} || 0),
	$function_total;
printf "  active project source files:                    %u\n",
	scalar keys %source_seen;
printf "  duplicate strong project symbols:               0\n";
printf "  active source-closure SHA-256:                   %s\n",
	sha256_hex($closure_material);
printf "  ELKS/XT-only retained SLOC by source:\n";
for my $source (sort {
	$elks_source_sloc{$b} <=> $elks_source_sloc{$a} || $a cmp $b
} keys %elks_source_sloc) {
	printf "    %5u  %s\n", $elks_source_sloc{$source}, $source;
}
printf "  largest ELKS/XT-only retained functions:\n";
my @elks_function = sort {
	$function_sloc{$b} <=> $function_sloc{$a} || $a cmp $b
} grep { $function_class{$_} eq 'elks' } keys %function_class;
my $function_limit = $ENV{LINEAGE_FUNCTION_LIMIT} || 20;
splice @elks_function, $function_limit
	if @elks_function > $function_limit;
for my $key (@elks_function) {
	my ($source, $name) = split /\0/, $key, 2;
	printf "    %5u  %s:%s\n", $function_sloc{$key}, $source, $name;
}
PERL
