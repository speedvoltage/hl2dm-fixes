# HL2DM Matchmaking (working title)

A community HL2DM mod preserving retail gameplay, built from
`test/all_fixes` at `9134c477953fc9fbda5f06e51562286e651825ab`.
Development branch: `feature/hl2mp-matchmaking`.

The intended experience is competitive duels and team deathmatch, a separate
casual mode, party lobbies, self-hosted dedicated servers, and a responsive
Half-Life 2-inspired orange/gray menu on Windows and Linux. A Steam mod release
with its own app ID is planned. HL2DM ownership is the proposed base-game
requirement, pending Valve's configuration/confirmation. The final name and
competitive team sizes are not decided.

## Current implementation

The native VGUI main menu attaches through `IGameUI::SetMainMenuOverride`.
It provides competitive/casual entry points, party controls, the retail server
browser and options, and in-game resume. `mm_menu 0` restores the retail menu
after a restart. No engine binary hooks are needed for this menu integration.

Steam party code supports asynchronous create/join/leave, overlay invitations,
`+connect_lobby` launch invitations, membership display, version checks, and
cleanup of cancelled requests. Successful multi-user parties are **not yet
verified**: live creation under SDK app ID 243750 returned Steam result 15
(Access Denied) while signed in. The UI reports that limitation. An app ID
owned by the project is not allocated yet.

The Python coordinator now implements authenticated sessions, parties, explicit
readiness, duel/2v2 queues, casual backfill, server reservations, ready checks,
per-player join grants, and transactional ratings from server-reported results.
Its Steam authentication boundary uses a publisher key on the service. Local
test accounts require an explicit development flag, a secret, and loopback.
See [the service setup and protocol](services/coordinator/README.md).

The native menu communicates through asynchronous Steam HTTP requests. It
supports solo queues, ready-check countdowns, acceptance/decline, cancellation,
server connection/reconnection, and leaving casual matches. SRCDS consumes
reservations, loads the assigned map, checks engine-authenticated Steam IDs
against private join grants, assigns teams, waits for the roster, and reports
results. These rules activate only for dedicated servers launched with
`-mm_server`; ordinary browser games retain their existing rules.

A real Windows client has completed a casual round on a real local dedicated
server, including admission, gameplay, result reporting, and reservation
release. The coordinator login used the explicit local development provider;
the game-server connection used the engine's Steam authentication. The native
builds and coordinator tests run on Windows and Linux. Linux runtime play,
multi-player competitive rounds, reconnect/forfeit behavior, and casual backfill
still need runtime verification.

Native Steam parties are not yet bridged to coordinator parties. Live
coordinator authentication needs the mod's own app ID and publisher key.
Multi-member Steam parties are blocked from queueing until synchronization
exists, so a party cannot silently queue solo. Lobby metadata is never proof
of a match assignment or result. This remains a development build.

## Project build and preview

Windows requirements: Git, Python 3.13+, Visual Studio 2022 C++ Build Tools
(v143 and Windows SDK), and Source SDK Base 2013 Multiplayer from Steam.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-windows.ps1
```

Linux requires Podman; the helper builds only the HL2MP game targets inside
Valve's Steam Runtime Sniper SDK container:

```bash
bash tools/build-linux.sh
```

Game libraries are published to `game/mod_hl2mp/bin/x64` and
`game/mod_hl2mp/bin/linux64`. Launch the installed SDK runtime with
`-game <absolute-path-to-game/mod_hl2mp> -windowed -w 1280 -h 720 -novid`.

Capture the visible Windows game canvas with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/capture-menu.ps1 -Name menu
```

Screenshots are saved in `artifacts/screenshots/` (local, not committed).
Debug commands: `mm_party_create`, `mm_party_leave`, `mm_party_status`,
`mm_queue duel|team|casual`, `mm_match_accept`, `mm_queue_cancel`.
The Windows preview launcher in the service guide runs a local ready check and
captures the game renderer directly, even when the game is not in the foreground.
`tools/preview-dedicated.py --verify` exercises a real local dedicated server
and a one-minute casual round; `--unreserved` checks identity mismatch rejection.

`tools/compare-engine-binaries.ps1` writes SHA-256 comparisons to
`artifacts/engine-comparison/windows-binaries.csv`. On 2026-10-31, SDK build
17413218 and retail HL2DM build 24600434 had 169 differing and 46 identical
Windows binaries, including differing engine/GameUI/VGUI/physics binaries.
This is a byte comparison, not a compatibility assessment; engine alignment is
deferred. Linux retail binaries have not been compared.

## Upstream Source SDK 2013

Source code for Source SDK 2013.

Contains the game code for Half-Life 2, HL2: DM and TF2.

**Now including Team Fortress 2! ✨**

## Build instructions

Clone the repository using the following command:

`git clone https://github.com/ValveSoftware/source-sdk-2013`

### Windows

Requirements:
 - Source SDK 2013 Multiplayer installed via Steam
 - Visual Studio 2022 with the following workload and components:
   - Desktop development with C++:
     - MSVC v143 - VS 2022 C++ x64/x86 build tools (Latest)
     - Windows 11 SDK (10.0.22621.0) or Windows 10 SDK (10.0.19041.1)
 - Python 3.13 or later

Inside the cloned directory, navigate to `src`, run:
```bat
createallprojects.bat
```
This will generate the Visual Studio project `everything.sln` which will be used to build your mod.

Then, on the menu bar, go to `Build > Build Solution`, and wait for everything to build.

You can then select the `Client (Mod Name)` project you wish to run, right click and select `Set as Startup Project` and hit the big green `> Local Windows Debugger` button on the tool bar in order to launch your mod.

The default launch options should be already filled in for the `Release` configuration.

### Linux

Requirements:
 - Source SDK 2013 Multiplayer installed via Steam
 - podman

Inside the cloned directory, navigate to `src`, run:
```bash
./buildallprojects
```

This will build all the projects related to the SDK and your mods automatically against the Steam Runtime.

You can then, in the root of the cloned directory, you can navigate to `game` and run your mod by launching the build launcher for your mod project, eg:
```bash
./mod_tf
```

*Mods that are distributed on Steam MUST be built against the Steam Runtime, which the above steps will automatically do for you.*

## Distributing your Mod

There is guidance on distributing your mod both on and off Steam available at the following link:

https://partner.steamgames.com/doc/sdk/uploading/distributing_source_engine

## Additional Resources

- [Valve Developer Wiki](https://developer.valvesoftware.com/wiki/Source_SDK_2013)

## License

The SDK is licensed to users on a non-commercial basis under the [SOURCE 1 SDK LICENSE](LICENSE), which is contained in the [LICENSE](LICENSE) file in the root of the repository.

For more information, see [Distributing your Mod](#markdown-header-distributing-your-mod).
