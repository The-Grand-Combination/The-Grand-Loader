# `join_port`

## Source

`patch/src/features/interface/join_port.cpp`.

## Why

Victoria II connects over UDP, always on port 1638: the host listens on it and the join dialog dials
it, whatever is typed. The dialog takes an IP address or a host name, with no port. Tunnel services
such as playit give the host a public address whose port is chosen by the service (for example
`209.25.140.16:6142`), so a friend cannot reach a host behind one. Typing `address:port` does not help
either: the game resolves the whole text as a name, fails, and connects nowhere without an error.

## How

- The call that builds the connection request in the join dialog goes to a thunk. When the typed text
  is `address:port` with a port from 1 to 65535, the request gets the address alone and that port;
  anything else reaches the game unchanged.
- Only the joining player needs it. The host keeps listening on 1638, which is where the tunnel's
  local side points (UDP, local port 1638).
- The typed text is not changed. Once connected, the lobby copies the session's host (the address
  alone) into the `lasthost` setting and saves it; that copy is given the full `address:port`
  instead, so the dialog starts with the port next time.
