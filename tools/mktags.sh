#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/.." && pwd)
output=${1:-"$root/TAGS"}

case "$output" in
  /*) ;;
  *) output=$PWD/$output ;;
esac

if ! command -v etags >/dev/null 2>&1; then
  printf '%s\n' 'error: etags is required to build TAGS' >&2
  exit 127
fi

file_list=$(mktemp "${TMPDIR:-/tmp}/janos-tags-files.XXXXXX")
tag_file=$(mktemp "$output.tmp.XXXXXX")

cleanup()
{
  rm -f "$file_list" "$tag_file"
}
trap cleanup EXIT

# Keep the index limited to the kernel. In particular, tools/src contains a
# complete vendored compiler tree and must not become part of TAGS.
git -C "$root" ls-files -co --exclude-standard -- kernel apps libc sdk |
awk '/\.[chSs]$/' > "$file_list"

if [ ! -s "$file_list" ]; then
  printf '%s\n' 'error: no kernel source files found' >&2
  exit 1
fi

(CDPATH= cd "$root" && etags --declarations --output="$tag_file" - < "$file_list")
mv -f "$tag_file" "$output"
printf 'Wrote %s from %d files\n' "$output" "$(wc -l < "$file_list")"
