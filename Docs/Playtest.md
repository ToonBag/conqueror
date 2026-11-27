# Playtesting with friends

How to run a Squad Command playtest over the internet before the real hosted backend (GDD §14) exists.
Friends run a packaged game client; the dedicated server runs either **on a small Google Cloud VM**
(recommended: nothing to set up on your router, nothing for friends to install, builds update themselves) or
**on your own PC** (free, but needs port forwarding or Tailscale).

It is the same server-authoritative setup as the local multiplayer environment (`CLAUDE.md`). The server is
the editor binary in `-server` mode, because the installed engine can't package a server; a source-built
engine would allow a small Linux server instead.

**Scripts need PowerShell to allow them.** This PC's execution policy blocks `.ps1` files by default. Either
run each one as `powershell -ExecutionPolicy Bypass -File .\Scripts\Playtest\Release.ps1`, or allow your
own scripts once with `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`. Friends' `Join.bat` already
bypasses it for its own launcher.

## Cloud server (recommended)

### What it is

| Piece | What it does |
|---|---|
| Windows Server VM (`e2-standard-2`, London) | Runs the dedicated server. Its agent (`Scripts/Playtest/Cloud/VmAgent.ps1`) keeps it on the newest build, restarts it if it crashes, and reports status. |
| Static IP + firewall rule | A fixed address open on UDP 17777 to everyone, so there are no friends' IPs to list. |
| `<project>-squad-server` bucket (private) | The server's files (engine ~15 GB once, project files per build), its settings (`server.json`) and status. |
| `<project>-squad-client` bucket | Game zips and `latest.json`, readable by anyone with the link (not listable). |

Strangers who find the address can join a match but can't reach anything else: the VM holds nothing personal.
Matches restart 30 s after a result, or as soon as every player has left.

**Cost** (approximate; check Google's pricing calculator): about $0.15–0.20 an hour while the VM runs
(including the Windows licence), so a 3-hour session costs well under $1. When stopped you still pay for the
disk (~$10/month) and the reserved IP (~$7/month). Each friend downloading a 550 MB build costs about $0.07.
New Google Cloud accounts usually get free trial credit. **Stop the VM after each session.**

### Once: set up Google Cloud (about 30 minutes)

1. Create a Google Cloud account at <https://console.cloud.google.com>, create a project (note its
   **project ID**, e.g. `squad-playtest-123456`) and turn on billing for it.
2. Install the Google Cloud CLI for Windows (<https://cloud.google.com/sdk/docs/install>), then in a new
   PowerShell: `gcloud auth login`.
3. Run the setup with your project ID. It creates the buckets, IP, firewall rule and VM, and writes
   `Scripts/Playtest/cloud.json` (not committed: it names your project):
   ```powershell
   .\Scripts\Playtest\CloudSetup.ps1 -ProjectId squad-playtest-123456
   ```
4. Publish the first build (below). The first one uploads the engine (~15 GB, about an hour on a typical home
   upload) and the VM then downloads it (10–20 min).

### Each build: release

Close the editor (Live Coding blocks the build), commit, then:

```powershell
.\Scripts\Playtest\Release.ps1
```

It builds the server code, packages the client, stamps both with one build ID (`<date>-<time>-<commit>`),
uploads what changed, switches the server over (it restarts within ~15 s, ending a match in progress) and
publishes the zip. About 5–15 minutes, unattended. Friends just run `Join.bat` again: it sees the new build,
downloads it, replaces their copy (keeping their settings and logs) and joins.

The server refuses any other build with a message telling the player to run `Join.bat` again, so nobody plays
a mismatched client.

### Each session

```powershell
.\Scripts\Playtest\CloudServer.ps1 -Start     # boot the VM; the server is up 2-3 minutes later
.\Scripts\Playtest\CloudServer.ps1            # status: build, players, recent joins / results
.\Scripts\Playtest\CloudServer.ps1 -Stop      # afterwards: stop paying for the VM
```

- **Settings:** the match starts once 2 players have joined (a bot plays any empty squad). Change with
  `CloudServer.ps1 -Players 4 -SquadsPerTeam 2` (2v2), `-NoBots` / `-Bots`; settings are kept for later
  releases (`Release.ps1 -Players ...` sets them too). Changing them restarts the server.
- **New friends:** send them the newest zip (`Playtest\SquadCommand-Playtest-<build>.zip`, or the download
  link `Release.ps1` prints). After that, `Join.bat` keeps them up to date.
- **Logs:** `CloudServer.ps1 -Log` shows the server log's end.

### When something goes wrong

- *Status says no report yet / stale:* the VM is still booting, or stopped. A brand-new VM waits for the first
  release, and after an engine upload it downloads ~15 GB before starting the server.
- *Friend sees an offline map with a red box:* the box says why (an old build: run `Join.bat` again).
- *Friend sees an offline map with no box:* the connection timed out: check the VM is running and the address.
- To look inside the VM: the agent's log is `C:\SquadServer\Logs\agent.log` and the server's
  `C:\SquadServer\Logs\Server.log`. You can open them over RDP (set a Windows password in the VM's page of
  the Cloud console and add a firewall rule for TCP 3389 from your own IP only).

## Home server (alternative)

Free, but friends must reach your PC: forward **UDP 17777** on your router to this PC (and allow it in the
Windows firewall, ideally only from friends' IPs), or have everyone install Tailscale. `HostServer.ps1`
checks the firewall and prints the rule to add.

```powershell
.\Scripts\Playtest\PackageClient.ps1 -HostAddress <your public or Tailscale IP>   # each build; send friends the zip
.\Scripts\Playtest\HostServer.ps1                              # 1v1, starts when 2 players have joined
.\Scripts\Playtest\HostServer.ps1 -SquadsPerTeam 2 -Players 4  # 2v2
.\Scripts\Playtest\HostServer.ps1 -Stop                        # when you are done
```

- `HostServer.ps1` only lets in the newest packaged build (`Playtest\last-build.json`); `-NoBuildCheck`
  lets any build in. It runs the C++ in `Binaries\`, so build the editor target first if you changed code;
  it warns when the source is newer.
- There is no self-update here: after a change, repackage and resend the zip.
- To play yourself, unzip the same build and run `Join.bat` with `127.0.0.1`.

## For friends

1. Unzip, run `Join.bat` (type the host's address if asked). It updates the game first when the host has
   published a newer build. If Windows says it protected your PC, click *More info* → *Run anyway*.
2. Seeing the map but nobody else means you are in an offline copy: a red box says why if the server refused
   you. Close it and run `Join.bat` again, or check with the host.
3. Bug reports: say what happened and when, and send `Unreal\Saved\Logs\Unreal.log` from the game folder.

## Limits of this setup

- No login: anyone who can reach the port can join (the cloud server holds nothing else of value).
- Every client is sent every soldier's position, even hidden ones (a cheater could read them). Fine among
  friends; to be fixed before public tests.
- Development build: everyone has the debug menu (F6) and console (`~`). The server refuses debug actions
  (heal, kill, teleport…) unless started with `squad.AllowDebugMenu 1`, but **Reveal all** only changes what a
  client draws, so it works for anyone (see the point above). Agree house rules.
- Friends download the whole client (~550 MB) for every build; smaller patches or Steam delivery come later.
