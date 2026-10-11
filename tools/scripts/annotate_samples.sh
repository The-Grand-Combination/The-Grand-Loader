#!/usr/bin/env bash
# Turns v2cp_samples.tsv (sample_profiler, collapsed stacks) into per-function tables.
#   tools/scripts/annotate_samples.sh <v2cp_samples.tsv> [zoom: function name or VA] [top N, default 40]
# Needs re/decomp/index.tsv (tools/ghidra/V2DecompAll.java, see re/ghidra.md) to map addresses to
# functions; names from re/symbols/functions.csv take precedence. Runs under Git Bash (gawk).
#
#   modules     where the leaf (EIP) was: v2game.exe or another DLL
#   exclusive   functions the leaf was in
#   inclusive   functions anywhere on the stack, counted once per stack (recursion is not double counted)
#   external    time spent in other modules, by the exe function that called out
#   zoom        for one function: its callers and its callees by inclusive time
set -euo pipefail

here="$(cd "$(dirname "$0")/../.." && pwd)"
samples="${1:?usage: annotate_samples.sh <v2cp_samples.tsv> [zoom] [top]}"
zoom="${2:-}"
top="${3:-40}"
index="$here/re/decomp/index.tsv"
csv="$here/re/symbols/functions.csv"
[ -f "$index" ] || { echo "re/decomp/index.tsv not found: run V2DecompAll.java first (re/ghidra.md)" >&2; exit 2; }

gawk -F'\t' -v zoom="$zoom" -v top="$top" -v csv="$csv" '
BEGIN {
    while ((getline line < csv) > 0) {
        split(line, f, ",")
        if (f[1] ~ /^0x/) csvname[tolower(f[1])] = f[2]
    }
}
FNR == NR {
    if (FNR == 1) next
    n++
    st[n] = strtonum($1)
    nm[n] = (tolower($1) in csvname) ? csvname[tolower($1)] : $2
    next
}
function find(a,    lo, hi, mid) {
    if (n == 0 || a < st[1]) return 0
    lo = 1; hi = n
    while (lo < hi) { mid = int((lo + hi + 1) / 2); if (st[mid] <= a) lo = mid; else hi = mid - 1 }
    return lo
}
# Name of a frame: a function of the exe, or a module.
function frame_name(f,    i) {
    if (f ~ /^m[0-9]+(:|$)/) { split(f, mp, ":"); return "[" modname[mp[1]] (mp[2] != "" ? "!" mp[2] : "") "]" }
    if (f == "?") return "[unknown]"
    i = find(strtonum(f))
    return i ? sprintf("%s 0x%08X", nm[i], st[i]) : sprintf("? %s", f)
}
function show(title, arr, limit,    k, x) {
    print "\n== " title
    k = 0
    for (x in arr) { if (++k > limit) break; printf "  %8.2f ms/day  %5.1f%%  %s\n", arr[x], 100 * arr[x] / total, x }
}
/^#/ { header = $0; next }
$1 == "mod" { modname[$2] = $3; next }
$1 == "stack" {
    w = $3
    total += w
    cnt = split($4, fr, ";")
    for (k = 1; k <= cnt; k++) name[k] = frame_name(fr[k])
    leaf = name[1]
    excl[leaf] += w
    if (fr[1] ~ /^m[0-9]+/ || fr[1] == "?") {
        split(fr[1], mp, ":"); modkey = fr[1] == "?" ? "[unknown]" : "[" modname[mp[1]] "]"
        module[modkey] += w
        if (cnt > 1) external[leaf " <- " name[2]] += w
    } else module["[v2game.exe]"] += w
    delete seen
    for (k = 1; k <= cnt; k++) if (!(name[k] in seen)) { seen[name[k]] = 1; incl[name[k]] += w }
    if (zoom != "") {
        # outermost occurrence of the zoomed function on this stack (recursion counted once)
        z = 0
        for (k = cnt; k >= 1; k--) if (index(name[k], zoom) == 1) { z = k; break }
        if (z) {
            zt += w
            callee[z > 1 ? name[z - 1] : "(self)"] += w
            caller[z < cnt ? name[z + 1] : "(root)"] += w
        }
    }
    next
}
END {
    print header
    printf "sampled: %.1f ms/day\n", total
    PROCINFO["sorted_in"] = "@val_num_desc"
    show("modules (leaf)", module, 30)
    show("exclusive, top " top, excl, top)
    show("inclusive (once per stack), top " top, incl, top)
    show("external modules by exe caller, top 25", external, 25)
    if (zoom != "") {
        printf "\n== zoom %s: %.2f ms/day inclusive\n", zoom, zt
        show("  callees of " zoom, callee, top)
        show("  callers of " zoom, caller, 15)
    }
}
' "$index" "$samples"
