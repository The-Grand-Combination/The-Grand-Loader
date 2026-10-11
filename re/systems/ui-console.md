# UI, console and rendering odds and ends

Sources: `Engine/engine-structure.md`, `Engine/buttons.md`, `Engine/conclusions.md` in
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering);
version label from this repo.

## Console

`Console_Execute` (`0x00420EB0`) is the whole console: tokenised input against a cascade of string
compares. Disabling it removes every command. **[C]** Useful entries: `debug cb_use <type> <TAG> <TAG>`
(prints `can_use.Describe`), `cb_countries`, `cb_state`, `fow`.

## Fog of war

`fow` toggles `g_fogOfWar` (`0x013F080C`) and mirrors it into `g_renderDevice+0x6BC44`. Setting the flag
alone does not keep fog off. NOP-ing branches in `Render_InitRestore` (`0x0099DA20`) or
`Render_MapFrame` (`0x006592F0`) breaks text/UI or terrain rendering respectively. The condition before
`0x006594B4` is the next thing to understand. No safe patch yet.

## Buttons

| function | |
|---|---|
| `Westernize_OnClick` `0x006DFE80` | C |
| `Westernize_CanClick` `0x00541B90` | C |
| `Play_OnClick` `0x00772300` (SP vs MP not distinguished) | I |

## Version label

`App_ctor` (`0x00633800`) builds `std::string("V2 v3.04", 8)` at `0x00633826`
(`PUSH 8; MOV EDI,0xF; PUSH 0x00E0764C`). The launcher patches the length and the pointer to append its
own text (`launcher/src/version_label.cpp`). The external "Master EXE Mod" instead overwrites the `4`
of the string with `5`, which is why it calls itself v3.05 while targeting this same exe.

## Views patched by V2DLL

V2DLL (an external DLL mod for the same exe) swaps vtable slots of `CTechnologyView` (`0x00E17FA4`,
slot 11 Update), `CBudgetView` (`0x00E059F0`, slot 10 tooltip), `CProductionView` (`0x00E0FECC`, slot
10), `CPoliticsView` (`0x00E0E458`, slot 10) and `CDecision` (`0x00E29B54`, slot 6). Useful as a list
of extension points; not reproduced here.
