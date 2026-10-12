# Year timer (always on)

## Source

`patch/src/features/profilers/year_timer.cpp`.

## Why

To time a run without a stopwatch and compare settings: how long a game year takes at the speed it was
played.

## How

- It hooks the game's hourly tick and watches the date. When the year changes, one line goes to
  `the_grand_loader.log`: wall time, active time (gaps of more than two seconds, i.e. pauses, removed), the
  number of days, and milliseconds per game day.
- A year with fewer than 365 days counted (usually the first one after loading a save) is marked as partial.
