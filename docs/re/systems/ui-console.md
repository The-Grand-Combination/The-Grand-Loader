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

## Chat (2026-10-17) **[C]**

- **Lines:** the in-game chat list is `chat_list` (`interface/menubar.gui`), lines `chat_entry` (with the
  sender's flag) and `chat_entry_other`. `Chat_OnMessage` (`0x00676E80`, listener of the chat channel)
  builds them; with message+0x24 = 0 it first checks the recipients list (+0x44) against the local
  player (private messages). It indexes the country list with message+0x54 unchecked, so a message
  needs a valid country there.
- **Expiry:** `Chat_ExpireLines` (`0x006784B0`, every frame from `CInGameIdler_Update`) drops the oldest
  line when the list is over its size, or when more than 60 s passed since the last removal
  (`CMP [EBP-4],0x3C` at `0x006784FD`; interface clock at this+0x1978 `vt[0x14]`, last removal at
  this+0xE0). One line per minute at most. Patched by `chat_line_seconds`.
- **Messages:** `ChatMessage_ctor` (`0x009F3290`): ECX parts list, EDX sender name, stack (message,
  flag country, key text, text, 1), `RET 0x14`; base constructor `0x009F2A50` (stdcall 5). The chat
  command: `ChatMessageCommand_ctor` (`0x009F4240`, stdcall (command, message, channel, name), vtable
  `0x00E3D100`, execute slot `0x009F4530`). The sender comes from `UI_GetState` (`0x0068B610`, the local
  player, name first), copied by `FUN_0064A460`.

## Text input **[C]**

The window procedure `App_WndProc` (`0x009DFC70`, through the stub `0x009E07F0`; class registered by
`FUN_009DFA30`) passes `WM_CHAR` to the focused widget (window+0x80: `vt[0x34]` takes text, `vt[0x28]`
inserts a character above 13) unless the input manager (`DAT_00F20C38+0xE8`, `vt[0x30]`) refuses it.
Ctrl+V arrives as `WM_CHAR` 0x16 and is dropped; characters sent while Ctrl was held did not reach the
field in a test, so the refusal most likely covers Ctrl **[I]**. The pump calls `DispatchMessageA`
through the import slot `0x00C8A310` (`0x009DF308`, `0x009DF6A9`). `clipboard_paste` posts the clipboard
text as `WM_CHAR` once Ctrl is up.

## Join dialog and settings **[C]**

- `JoinIpDialog_Open` (`0x00766B80`, title `JOIN_IP_TITLE`) fills the `edit` box with the `lasthost`
  setting; `JoinIpDialog_OnJoin` (`0x00767140`) reads it back, stores it in `lasthost` (`0x00767328`) and
  builds the connection request with port 1638 ([network.md](network.md)).
- `CSettings` (singleton `g_settings` `0x0125B5E0`, `Settings_Get` `0x00475500`): `lastplayer` +0x108,
  `lasthost` +0x124, `serveradress` +0x140, file name +0xC0; tokens 0x27E-0x280 parsed by slot 4
  (`0x00475750`), written by slot 2 (`0x00475AF0`). One `settings.txt` per mod, under
  `Documents\Paradox Interactive\Victoria II\<mod>\`.
- `Settings_Save` (`0x009D7FA0`, settings in ESI) writes the file; callers are four menus, the
  destructor (not reached by a normal exit) and `Lobby_SaveSettings` (`0x0076BA60`), which on entering
  the lobby as a client copies the session's host into `lasthost` and saves. `save_last_ip` saves right
  after Join as well; `join_port` keeps the port in the copied value.

## Views patched by V2DLL

V2DLL (an external DLL mod for the same exe) swaps vtable slots of `CTechnologyView` (`0x00E17FA4`,
slot 11 Update), `CBudgetView` (`0x00E059F0`, slot 10 tooltip), `CProductionView` (`0x00E0FECC`, slot
10), `CPoliticsView` (`0x00E0E458`, slot 10) and `CDecision` (`0x00E29B54`, slot 6). Useful as a list
of extension points; not reproduced here.
