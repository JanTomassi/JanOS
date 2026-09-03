#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/.." && pwd)

usage()
{
  printf '%s\n' \
    'usage: tools/ksearch.sh [kind] identifier' \
    '       tools/ksearch.sh identifier' \
    '' \
    'kinds: symbol, function, variable, struct, typedef, enum, macro'
}

if ! command -v rg >/dev/null 2>&1; then
  printf '%s\n' 'error: rg is required for kernel searches' >&2
  exit 127
fi

case "$#" in
  1)
    case "$1" in
      -h|--help)
        usage
        exit 0
        ;;
    esac
    kind=symbol
    query=$1
    ;;
  2)
    kind=$1
    query=$2
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac

case "$kind" in
  -h|--help)
    usage
    exit 0
    ;;
esac

if [ -z "$query" ]; then
  usage >&2
  exit 2
fi

# Declaration searches expect a C identifier. Literal symbol searches may
# instead use any non-empty string, which is useful for constants or paths.
case "$kind" in
  symbol|all) ;;
  *)
    case "$query" in
      *[!A-Za-z0-9_]* )
        printf '%s\n' 'error: declaration searches require a C identifier' >&2
        exit 2
        ;;
    esac
    ;;
esac

case "$kind" in
  symbol|all)
    pattern=$query
    fixed=yes
    ;;
  function|functions|func)
    pattern="^[[:space:]]*(\\[\\[[^]]*\\]\\][[:space:]]+|[[:alnum:]_]+[[:space:]]+|\\*[[:space:]]*)+${query}[[:space:]]*\\("
    fixed=no
    ;;
  variable|variables|var)
    pattern="^[[:space:]]*(static[[:space:]]+|extern[[:space:]]+|const[[:space:]]+|volatile[[:space:]]+|register[[:space:]]+|unsigned[[:space:]]+|signed[[:space:]]+|short[[:space:]]+|long[[:space:]]+|struct[[:space:]]+[[:alnum:]_]+[[:space:]]+|union[[:space:]]+[[:alnum:]_]+[[:space:]]+|enum[[:space:]]+[[:alnum:]_]+[[:space:]]+|[[:alnum:]_]+[[:space:]]+|\\*[[:space:]]*)+${query}([[:space:]]*\\[[^]]*\\])?[[:space:]]*(=|;|,)"
    fixed=no
    ;;
  struct|union)
    pattern="^[[:space:]]*(typedef[[:space:]]+)?(struct|union)[[:space:]]+${query}[[:space:]]*([{;]|$)"
    fixed=no
    ;;
  typedef)
    pattern="(^[[:space:]]*typedef.*\\b${query}\\b|^[[:space:]]*}[[:space:]]*${query}\\b)"
    fixed=no
    ;;
  enum)
    pattern="^[[:space:]]*(typedef[[:space:]]+)?enum[[:space:]]+${query}([[:space:]{;]|$)"
    fixed=no
    ;;
  macro)
    pattern="^[[:space:]]*#[[:space:]]*define[[:space:]]+${query}([[:space:](]|$)"
    fixed=no
    ;;
  *)
    printf 'error: unknown search kind: %s\n' "$kind" >&2
    usage >&2
    exit 2
    ;;
esac

CDPATH= cd "$root"

search()
{
  rg --no-heading --line-number --column --color=never \
    --glob '*.c' --glob '*.h' --glob '*.s' --glob '*.S' \
    "$@" -- kernel/include kernel/src
}

if [ "$fixed" = yes ]; then
  search --fixed-strings -e "$pattern"
else
  search -e "$pattern"
fi
