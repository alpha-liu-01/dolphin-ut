# Dolphin on Ubuntu Touch: the port's notes

These are the working notes of the Ubuntu Touch port: what was probed, built, decided and tested,
stage by stage, with the on-screen test results. To build the port, start with
[UbuntuTouchSetup.md](../../UbuntuTouchSetup.md) instead.

Start with [roadmap.md](roadmap.md): the overview (the target phone, how Dolphin maps onto it, the
rules for this fork) and every stage's goal and status.

| Document | What it covers |
| --- | --- |
| [roadmap.md](roadmap.md) | Overview, the stages and their status, risks |
| [wayland-prior-work.md](wayland-prior-work.md) | Existing Wayland work for Dolphin (upstream PR #14962 and others) |
| [stage0-probe.md](stage0-probe.md) | Probing the phone: Wayland, EGL, Vulkan, Qt 6, gamepads |
| [stage1-build.md](stage1-build.md) | The cross-build container and a CPU-only run |
| [stage2-wayland.md](stage2-wayland.md) | Wayland rendering on Lomiri, from PR #14962 |
| [stage3-roadmap.md](stage3-roadmap.md) | The touch-first QML frontend's plan |
| [stage3.0-foundations.md](stage3.0-foundations.md) … [stage3.8-defaults.md](stage3.8-defaults.md) | The frontend: library, emulation screen, touch overlay, in-game menu, controllers, settings, storage, defaults |
| [stage4-adreno.md](stage4-adreno.md) | Performance and correctness on the Adreno 619; custom GPU drivers |
| [stage5-packaging.md](stage5-packaging.md) | The click package, confinement, fullscreen under Lomiri |
| [stage6-roadmap.md](stage6-roadmap.md) | Feature completeness: the phases 6.1–6.8 |
| [stage6.1-ingame-settings.md](stage6.1-ingame-settings.md) | Settings during a game; Dolphin's questions |
| [stage6.2-remaining-settings.md](stage6.2-remaining-settings.md) | Hotkeys, post-processing, logging, custom RTC, debug actions |
| [stage6.3-wii-files.md](stage6.3-wii-files.md) | WADs, Wii saves, NAND backups, the Wii Menu, system updates |
| [stage6.4-game-tools.md](stage6.4-game-tools.md) | Cheats, Riivolution, disc conversion |
| [stage6.5-user-data.md](stage6.5-user-data.md) | Backup and restore of the user data; files in and out |
| [stage6.6-motion.md](stage6.6-motion.md) | Wii Remote motion from the phone's sensors (sensorfwd) |
| [stage6.7-achievements.md](stage6.7-achievements.md) | RetroAchievements |
| [stage6.8-leftovers.md](stage6.8-leftovers.md) | Driver zips from other apps, restarting, Skylanders and Infinity Base, the scrolling menu |
| [stage7-release.md](stage7-release.md) | Release follow-ups: the menu button, the release build without development aids, the click for 24.04-1.x, what is left |

Each stage document gives its goal, how it was built, what was checked over adb, the manual
on-screen tests and their results, and the decisions taken along the way.
