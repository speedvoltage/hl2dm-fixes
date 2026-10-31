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

Queue buttons currently report that the matchmaking service is not connected.
Server reservation, authentication, competitive results/ratings, and end-to-end
matchmaking remain to be implemented. Lobby metadata must not be accepted as
proof of a match assignment or match result. The coordinator and dedicated
server will establish those facts.

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
Debug commands: `mm_party_create`, `mm_party_leave`, `mm_party_status`.

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
