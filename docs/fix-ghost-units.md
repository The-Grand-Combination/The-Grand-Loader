# `fix_ghost_units`

## Source

`patch/src/features/stability/ghost_units.cpp`. Research: [re/systems/diplomacy.md](re/systems/diplomacy.md)
("Units of a dead tag").

## Why

When a country loses its last province through an annexation, the game deletes its armies and fleets.
It also loses provinces in other ways: a `secede_province` effect, a release, or a province transfer in a
peace deal. The province transfer itself never deletes units. `secede_province` deletes only the old owner's
armies that have no regiments left, and the other ways delete nothing. A country that dies like that keeps
every army and fleet where it stands, often inside other countries. Nobody owns them any more: the
country's AI does not run for a country without provinces, and no player controls it. These are the
ghost units.

## How

- Once a day, just before the day's tick, every country without provinces that still has units gets
  them deleted with the game's own deletion (`World_DeleteArmy`), the same loop an annexation runs.
  The deletion unloads fleets, leaves battles and returns the regiments' soldiers to their pops.
- Rebels (`REB`) are left alone. They never own provinces and own every rebel army.
- A save that already has ghost units is cleaned on the first day after loading.
- Each cleanup writes one line in `the_grand_loader.log` with the date, the tag and the unit count, up to
  64 lines per session.
- It changes the game only where vanilla leaves ghosts. Multiplayer: every player needs the same value.
