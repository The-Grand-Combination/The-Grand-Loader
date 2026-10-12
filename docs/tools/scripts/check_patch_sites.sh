#!/usr/bin/env bash
# Checks the `original` bytes of every row of docs/re/symbols/patch_sites.csv against a v2game.exe.
#   docs/tools/scripts/check_patch_sites.sh [path/to/v2game.exe]    (default: $V2_GAME_DIR/v2game.exe)
# Prints one line per row (OK / DIFF / SKIP when the row has no original bytes) with the file offset,
# and exits non-zero if any row differs. Runs under Git Bash; needs only od and gawk.
set -euo pipefail

here="$(cd "$(dirname "$0")/../../.." && pwd)"  # repo root (this file is docs/tools/scripts/)
exe="${1:-${V2_GAME_DIR:-}/v2game.exe}"
csv="$here/docs/re/symbols/patch_sites.csv"
[ -f "$exe" ] || { echo "v2game.exe not found: $exe" >&2; exit 2; }

# v2game.exe 3.04 section table: file offset = VA - delta.
to_offset() {
    local va=$(( $1 ))
    if   (( va >= 0x00401000 && va < 0x00C8A000 )); then echo $(( va - 0x00400C00 ))  # .text
    elif (( va >= 0x00C8A000 && va < 0x00EF2000 )); then echo $(( va - 0x00401A00 ))  # .rdata
    elif (( va >= 0x00EF2000 && va < 0x00F1FA00 )); then echo $(( va - 0x00402600 ))  # .data (initialised part)
    else echo -1; fi
}

fail=0
while IFS=$'\t' read -r id va original; do
    if [ -z "$original" ]; then printf 'SKIP  %-48s %s\n' "$id" "$va"; continue; fi
    off=$(to_offset "$va")
    if (( off < 0 )); then printf 'SKIP  %-48s %s (not file-backed)\n' "$id" "$va"; continue; fi
    n=$(wc -w <<< "$original")
    got=$(od -An -tx1 -v -j "$off" -N "$n" "$exe" | tr -s ' \n' ' ' | sed 's/^ //; s/ $//' | tr a-f A-F)
    if [ "$got" = "$original" ]; then
        printf 'OK    %-48s %s  file 0x%X\n' "$id" "$va" "$off"
    else
        printf 'DIFF  %-48s %s  file 0x%X  expected [%s] found [%s]\n' "$id" "$va" "$off" "$original" "$got"
        fail=1
    fi
done < <(gawk -v FPAT='([^,]*)|("[^"]*")' -v OFS='\t' 'NR > 1 && NF { print $1, $2, $3 }' "$csv")

exit $fail
