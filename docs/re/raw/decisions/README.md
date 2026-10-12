# Static analysis of decision scripts (2026-10-03)

Output of `docs/tools/scripts/decision_once_pattern.pl` and `decision_dead_patterns.pl` run on the game's
`mod/TGC` (the mod of all profiler runs; 1653 decisions, matches the 1651 counted at runtime) and `mod/GFM`.

- `*-once-decisions.txt`: decisions whose effect sets country flag F, whose `potential` has a top-level
  `NOT = { has_country_flag = F }`, and where no `clr_country_flag = F` exists anywhere in the mod
  (decisions, events, common, technologies, inventions, history). Columns: name, flag, tag_locked
  (top-level `tag = XXX`), set_top (flag set unconditionally at the top of the effect).
- `*-dead-patterns.txt`: per-decision count of monotonic conditions in `potential` (once a decision is
  dead for a country by one of them, it stays dead), plus the first condition of every `potential`.

Monotonic conditions counted: once_country_flag, once_global_flag (NOT has_global_flag, flag set somewhere,
never cleared), after_year (NOT year = N), not_civilized, unreachable flags (required but never set),
unknown_tag, invention/tech. `tag_locked` is not a death pattern but bounds the decision to one country.
