# Playtesting with friends

How to run a Squad Command playtest over the internet before there is a hosted server: **you host a
dedicated server on your PC, and friends run a packaged game client that connects to it.** It is the same
server-authoritative setup as the local multiplayer environment (`CLAUDE.md`), with friends' PCs as the
clients.

## Once: get a connection your friends can reach

Pick one:

- **Tailscale (recommended).** Free. You and each friend install it and join your tailnet (you invite them
  from the Tailscale admin page). Friends connect to your Tailscale address (`100.x.y.z`). No router changes,
  and nobody outside your tailnet can reach the server. ZeroTier works the same way.
- **Port forwarding.** Forward **UDP 17777** on your router to this PC and give friends your public IP.
  Anyone who finds the address can connect, and there is no login yet, so only do this for the session and
  only share the address with the people playing.

Windows must let `UnrealEditor.exe` receive UDP 17777. `HostServer.ps1` checks, and if there is no rule it
prints a command to run in an admin PowerShell that allows it from Tailscale addresses only (any network
profile, since Windows may class the Tailscale adapter as Public). If Windows shows its own prompt instead,
tick both Private and Public.

## Each build: package the client and send it

```powershell
.\Scripts\Playtest\PackageClient.ps1 -HostAddress 100.101.102.103
```

- `-HostAddress` bakes your address into the friends' `Join.bat`. Leave it out and `Join.bat` asks for it.
- Output: `Playtest\SquadCommand-Playtest-<date>-<commit>.zip` (Win64, Development build). Send the zip with
  any file-sharing service.
- The first package cooks every shader and takes a while; later ones are much quicker.
- **Everyone must run the same build as your server.** The zip name carries the commit; after any code or
  content change, build, repackage and resend.

## Each session: host

```powershell
.\Scripts\Playtest\HostServer.ps1                              # 1v1, starts when 2 players have joined
.\Scripts\Playtest\HostServer.ps1 -SquadsPerTeam 2 -Players 4  # 2v2
.\Scripts\Playtest\HostServer.ps1 -Stop              # when you are done
```

It starts the server minimised, prints the addresses to give out, and reports players joining, the
teams they get, bots handing squads over, and the result. Ctrl+C only stops watching; `-Stop` stops the
server. Log: `Saved\Logs\Playtest_Server.log`.

To play yourself, unzip the same build and run `Join.bat` with `127.0.0.1`.

- The match starts once `-Players` players have joined (default **2**); until then nobody fights. Set it to
  how many people are playing, or a bot plays the missing side from the first join until the last friend
  arrives (packaged clients take a while to load).
- Players go to the side with fewer players; a squad no player commands is played by a bot (`-NoBots` leaves
  it standing).
- The server runs the C++ build in `Binaries\` (the installed engine can't package a server), so build the
  editor target before hosting if you changed code. The script warns when the source is newer.

## For friends

1. Unzip, run `Join.bat` (type the host's address if asked).
2. Seeing the map but nobody else means the connection failed and you are in a single-player copy: close
   it and check the address and the host's server.
3. Bug reports: say what happened and when, and send `Unreal\Saved\Logs\Unreal.log` from the game folder.

## Limits of this setup

- Hosting on your PC: your upload speed and uptime decide the session. A hosted dedicated server
  (GDD §14) replaces this once there is a source-built engine.
- No login: anyone who can reach the port can join. Prefer Tailscale.
- Every client is sent every soldier's position, even hidden ones (a cheater could read them). Fine among
  friends; to be fixed before public tests.
- Development build: everyone has the debug menu (F6) and console (`~`). The server refuses debug actions
  (heal, kill, teleport…) unless you start it with `squad.AllowDebugMenu 1`, but **Reveal all** only changes
  what a client draws, so it works for anyone (see the point above). Agree house rules.
