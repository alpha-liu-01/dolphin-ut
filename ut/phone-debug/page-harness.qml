// Opens one of dolphin-ut's pages for a screenshot (stage 3.1), over the library page:
//   sh run-dolphin.sh ut 9 --qml /home/phablet/dolphin-ut-probe/page-harness.qml --test-page NAME \
//     [--test-arg GAME_PATH] --grab /home/phablet/dolphin-ut-probe/page.png --grab-after 6000
// NAME: details (needs GAME_PATH), folders, picker, developer, controllers, gcport, wiimote,
// mapping-gc, mapping-wii, settings:<MENU TAG>, game-settings (--test-arg GAMEID),
// settings-roundtrip, file-picker, import, userdata, paths-roundtrip, sd-roundtrip, gpu-drivers,
// gpu-roundtrip (--test-arg ZIP; ZIP.nometa.zip is the failure case), hotkeys,
// remaining-roundtrip, hotkeys-map, log-sync, settings-tap:MENU (--test-arg ROW) (stage 6.2);
// wii, wii-roundtrip (stage 6.3); cheats-roundtrip, riivolution, convert, convert-run (stage 6.4);
// userdata-export, userdata-check, userdata-import, userdata-reset, print-setting, share (--test-arg FILE), userdata (stage 6.5); motion (--test-arg MODE) (stage 6.6); achievements (--test-arg on|off) (stage 6.7);
// anything else
// shows the library.
import QtQuick
import QtQuick.Window
import Lomiri.Components

Window {
    visible: true
    width: units.gu(50); height: units.gu(80)
    color: theme.palette.normal.background
    MainView {
        anchors.fill: parent
        applicationName: "dolphin-ut"
        PageStack {
            id: stack
            Component.onCompleted: {
                const a = Qt.application.arguments
                const page = a[a.indexOf("--test-page") + 1]
                const arg = a.indexOf("--test-arg") >= 0 ? a[a.indexOf("--test-arg") + 1] : ""
                const base = "qrc:/qt/qml/DolphinUT/qml/"
                stack.push(base + "LibraryPage.qml")
                timer.page = page; timer.arg = arg; timer.base = base
                timer.start()
            }
        }
    }
    property var testMapping
    Timer {
        id: timer2
        function start(ms) { interval = ms; running = true }
        onTriggered: { console.log("CONVERT cancel"); discConverter.cancel() }
    }
    Timer {
        id: timer
        property string page; property string arg; property string base
        interval: 2500
        onTriggered: {
            if (page === "details") stack.push(base + "GameDetailsPage.qml", { details: gameList.details(arg) })
            else if (page === "folders") stack.push(base + "GameFoldersPage.qml")
            else if (page === "picker") { stack.push(base + "GameFoldersPage.qml"); stack.push(base + "FolderPickerPage.qml") }
            else if (page === "developer") stack.push(base + "DeveloperPage.qml")
            else if (page === "controllers") stack.push(base + "ControllersPage.qml")
            else if (page === "gcport") stack.push(base + "GameCubePortPage.qml", { port: arg ? Number(arg) : 0 })
            else if (page === "gcport-open") {
                // The type selector opened, as after a tap.
                stack.push(base + "GameCubePortPage.qml", { port: arg ? Number(arg) : 1 })
                const find = (item) => {
                    if (item.objectName === "typeSelector") return item
                    for (let i = 0; i < item.children.length; ++i) {
                        const found = find(item.children[i])
                        if (found) return found
                    }
                    return null
                }
                const selector = find(stack.currentPage)
                console.log("selector " + selector + " model " + (selector ? selector.model : ""))
                if (selector) selector.expanded = true
            }
            else if (page === "wiimote") stack.push(base + "WiimotePage.qml", { wiimote: 0 })
            else if (page === "mapping-gc") stack.push(base + "MappingPage.qml", { mapping: controllers.mapping("gcpad", 0) })
            else if (page === "file-picker") stack.push(base + "FolderPickerPage.qml", { title: "Open File", pickFiles: true, extensions: ["iso", "rvz", "wbfs", "gcm"], startFolder: arg })
            else if (page === "import") stack.push(base + "ImportPage.qml")
            else if (page === "userdata") stack.push(base + "UserDataPage.qml")
            else if (page === "gpu-drivers") stack.push(base + "GpuDriversPage.qml")
            else if (page === "gpu-roundtrip") {
                // Stage 4: install the driver package --test-arg ZIP and choose it (as the page does),
                // list; then the
                // failure cases (not a zip, a zip without meta.json).
                const r = gpuDrivers.install(arg)
                console.log("GPU install " + JSON.stringify(r))
                if (r.ok)
                    gpuDrivers.select(r.id)
                console.log("GPU active '" + gpuDrivers.active + "' error '" + gpuDrivers.activeError + "' selected '" + gpuDrivers.selected + "'")
                console.log("GPU drivers " + JSON.stringify(gpuDrivers.drivers))
                console.log("GPU not a zip " + JSON.stringify(gpuDrivers.install("/etc/hostname")))
                console.log("GPU no meta " + JSON.stringify(gpuDrivers.install(arg + ".nometa.zip")))
                stack.push(base + "GpuDriversPage.qml")
            }
            else if (page === "paths-roundtrip") {
                // A path setting (3.7): set, read back, reset (--test-arg FOLDER).
                const name = "StringSetting.MAIN_DUMP_PATH"
                console.log("PATHS default " + settingsModel.value(name))
                settingsModel.setValue(name, arg)
                console.log("PATHS set " + settingsModel.value(name) + " frames folder " + storage.exists(arg + "/Frames"))
                settingsModel.setValue(name, "")
                console.log("PATHS reset " + settingsModel.value(name))
                console.log("PATHS sd image " + settingsModel.value("StringSetting.MAIN_WII_SD_CARD_IMAGE_PATH") + " places " + JSON.stringify(storage.places().map(p => p.name)))
            }
            else if (page === "sd-roundtrip") {
                // The Wii SD card actions: pack the sync folder into the image, then unpack it.
                settingsModel.actionFinished.connect(function (action, ok) {
                    console.log("SD " + action + " " + ok)
                    if (action === "sd_pack") settingsModel.runAction("sd_unpack")
                })
                console.log("SD start " + settingsModel.runAction("sd_pack"))
            }
            else if (page.startsWith("settings-tap:")) {
                // A settings menu with one of its rows tapped (its dialog open), and the list moved to
                // it: --test-page settings-tap:MENU --test-arg "ROW TITLE" (stage 6.2).
                stack.push(base + "SettingsPage.qml", { menuTag: page.substring(13) })
                const find = (item) => {
                    if (item.entry !== undefined && item.entry && item.entry.title === arg && item.clicked) return item
                    for (let i = 0; i < item.children.length; ++i) {
                        const found = find(item.children[i])
                        if (found) return found
                    }
                    return null
                }
                Qt.callLater(() => {
                    const row = find(stack.currentPage)
                    console.log("TAP " + arg + ": " + row)
                    if (!row) return
                    let flick = row.parent
                    while (flick && flick.contentY === undefined) flick = flick.parent
                    if (flick) flick.contentY = Math.max(0, Math.min(row.mapToItem(flick.contentItem, 0, 0).y - units.gu(10), flick.contentHeight - flick.height))
                    row.clicked()
                    // Whether the dialog can scroll its contents (a long choice list).
                    Qt.callLater(() => {
                        const walk = (item, depth) => {
                            if (item.contentHeight !== undefined && item.flicking !== undefined && item.visible)
                                console.log("FLICK depth " + depth + " h " + Math.round(item.height) + " content " + Math.round(item.contentHeight) + " interactive " + item.interactive)
                            for (let i = 0; i < item.children.length; ++i) walk(item.children[i], depth + 1)
                        }
                        let top = stack
                        while (top.parent) top = top.parent
                        walk(top, 0)
                    })
                })
            }
            else if (page.startsWith("settings:")) stack.push(base + "SettingsPage.qml", { menuTag: page.substring(9) })
            else if (page === "game-settings") {
                // --test-arg GAMEID: that game's settings
                if (settingsModel.openGame(arg, 0))
                    stack.push(base + "SettingsPage.qml", { closesGame: true, gameTitle: arg })
            }
            else if (page === "game-roundtrip") {
                // A game setting (--test-arg GAMEID): the backend for that game only.
                const name = "StringSetting.MAIN_GFX_BACKEND"
                console.log("GAME global before " + settingsModel.value(name))
                console.log("GAME open " + settingsModel.openGame(arg, 0))
                settingsModel.setValue(name, "OGL")
                console.log("GAME in game " + settingsModel.value(name) + " overridden " + settingsModel.overridden(name))
                settingsModel.closeGame()
                console.log("GAME global after " + settingsModel.value(name))
            }
            else if (page === "settings-roundtrip") {
                // Write a setting, read it back, and print both (for the test copy of the config).
                const name = "BooleanSetting.MAIN_CPU_THREAD"
                console.log("ROUNDTRIP before " + settingsModel.value(name))
                settingsModel.setValue(name, !settingsModel.value(name))
                console.log("ROUNDTRIP after " + settingsModel.value(name))
                console.log("ROUNDTRIP boost " + settingsModel.value("Virtual.CPU_BOOST") + " dsp " + settingsModel.value("Virtual.DSP_ENGINE") + " mem1 " + settingsModel.value("Virtual.MEM1_MB") + " backend " + settingsModel.value("StringSetting.MAIN_GFX_BACKEND") + " speed " + settingsModel.value("FloatSetting.MAIN_EMULATION_SPEED"))
            }
            else if (page === "detect") {
                // Input detection on every device: write "PRESS A" into a Pipes/ FIFO meanwhile.
                testMapping = controllers.mapping("gcpad", 0)
                testMapping.detected.connect((g, c, e) => console.log("DETECTED group " + g + " control " + c + ": " + e))
                testMapping.detect(0, 0, true)
                console.log("DETECTING")
            }
            else if (page === "mapping-wii") stack.push(base + "MappingPage.qml", { mapping: controllers.mapping("wiimote", 0, -1, [3]) })
            else if (page === "hotkeys") {
                // Stage 6.2: the hotkey page, and its groups with the number of hotkeys in each.
                testMapping = controllers.mapping("hotkeys", 0)
                console.log("HOTKEYS device " + testMapping.device + " groups " + JSON.stringify(testMapping.groups.map(g => g.name + ":" + g.controls.length)))
                console.log("HOTKEYS first " + JSON.stringify(testMapping.groups[0].controls))
                stack.push(base + "HotkeysPage.qml")
            }
            else if (page === "wii") stack.push(base + "WiiSystemPage.qml")
            else if (page === "userdata-export") {
                // Stage 6.5: marker speed 1.5, export to --test-arg ZIP, then speed 1.0 and a file
                // copied into StateSaves (both should be undone by importing the export).
                const speed = "FloatSetting.MAIN_EMULATION_SPEED"
                settingsModel.setValue(speed, 1.5)
                userData.finished.connect(function (task, ok, cancelled, message) {
                    console.log("USERDATA " + task + " ok " + ok + " cancelled " + cancelled + " '" + message + "'")
                    settingsModel.setValue(speed, 1.0)
                    console.log("USERDATA copy '" + userData.copyIn("/home/phablet/dolphin-ut-probe/ut-env.sh", storage.userFolder + "/StateSaves", false) + "'")
                    console.log("USERDATA copy again '" + userData.copyIn("/home/phablet/dolphin-ut-probe/ut-env.sh", storage.userFolder + "/StateSaves", false) + "'")
                    console.log("USERDATA copy outside '" + userData.copyIn("/home/phablet/dolphin-ut-probe/ut-env.sh", "/home/phablet", false) + "'")
                })
                console.log("USERDATA into user folder refused " + !userData.exportTo(storage.userFolder + "/x.zip"))
                console.log("USERDATA export start " + userData.exportTo(arg) + " backup folder " + userData.backupFolder + " new " + userData.newBackupPath())
                stack.push(base + "UserDataPage.qml")
            }
            else if (page === "userdata-check") {
                // --test-arg FOLDER with notzip.zip, noini.zip, traversal.zip; then imports traversal.zip.
                for (const f of ["notzip.zip", "noini.zip", "traversal.zip"])
                    console.log("USERDATA check " + f + " '" + userData.checkBackup(arg + "/" + f) + "'")
                userData.finished.connect((task, ok, cancelled, message) => console.log("USERDATA " + task + " ok " + ok + " '" + message + "'"))
                console.log("USERDATA import traversal start " + userData.importFrom(arg + "/traversal.zip"))
            }
            else if (page === "userdata-import") {
                // --test-arg ZIP: import, then quit as the dialog's button does.
                console.log("USERDATA speed before import " + settingsModel.value("FloatSetting.MAIN_EMULATION_SPEED"))
                userData.finished.connect(function (task, ok, cancelled, message) {
                    console.log("USERDATA " + task + " ok " + ok + " '" + message + "'")
                    if (ok) userData.quitNow()
                })
                console.log("USERDATA import start " + userData.importFrom(arg))
            }
            else if (page === "userdata-reset") {
                console.log("USERDATA speed before reset " + settingsModel.value("FloatSetting.MAIN_EMULATION_SPEED") + " dual core " + settingsModel.value("BooleanSetting.MAIN_CPU_THREAD"))
                console.log("USERDATA reset " + userData.resetSettings())
                console.log("USERDATA speed after reset " + settingsModel.value("FloatSetting.MAIN_EMULATION_SPEED") + " dual core " + settingsModel.value("BooleanSetting.MAIN_CPU_THREAD"))
            }
            else if (page === "share") stack.push(base + "SharePage.qml", { path: arg })
            else if (page === "userdata") stack.push(base + "UserDataPage.qml")
            else if (page === "motion") {
                // Stage 6.6: Wii Remote 1's motion choice (--test-arg MODE: 0 with pointer, 1 without,
                // 2 none), read back; then the Wii Remote page.
                console.log("MOTION before " + controllers.wiimoteMotion(0))
                if (arg !== "") controllers.setWiimoteMotion(0, Number(arg))
                console.log("MOTION after " + controllers.wiimoteMotion(0))
                stack.push(base + "WiimotePage.qml", { wiimote: 0 })
            }
            else if (page === "achievements") {
                // Stage 6.7: --test-arg on|off sets Enable Achievements, then the menu.
                const name = "BooleanSetting.ACHIEVEMENTS_ENABLED"
                console.log("ACH before enabled " + settingsModel.value(name) + " loggedIn " + achievements.loggedIn + " hardcore " + achievements.hardcoreActive)
                if (arg !== "") settingsModel.setValue(name, arg === "on")
                console.log("ACH after enabled " + settingsModel.value(name) + " loggedIn " + achievements.loggedIn + " hardcore " + achievements.hardcoreActive + " user '" + achievements.username + "'")
                stack.push(base + "SettingsPage.qml", { menuTag: "CONFIG_ACHIEVEMENTS" })
            }
            else if (page === "print-setting") console.log("SETTING " + arg + " = " + settingsModel.value(arg))
            else if (page === "cheats-roundtrip") {
                // Stage 6.4: a game's codes (--test-arg GAMEID, --test-arg2 GAMETDBID): counts, then
                // add, check, toggle, edit and delete one code of each kind, then download Gecko codes.
                const a = Qt.application.arguments
                const tdb = a.indexOf("--test-arg2") >= 0 ? a[a.indexOf("--test-arg2") + 1] : arg
                const count = () => ["patch", "ar", "gecko", "mod"].map(t => t + " " + cheats.items.filter(i => i.type === t).length).join(", ")
                console.log("CHEATS load " + cheats.load(arg, 0, tdb) + ": " + count())
                console.log("CHEATS first " + JSON.stringify(cheats.items[0]))
                console.log("CHEATS bad name '" + cheats.add("patch", "", "", "", "0x80001234:dword:0x00000000") + "'")
                console.log("CHEATS bad line '" + cheats.add("patch", "T", "", "", "0x80001234:dword:0x00000000\nnonsense") + "'")
                console.log("CHEATS empty '" + cheats.add("gecko", "T", "", "", "\n") + "'")
                console.log("CHEATS mixed AR '" + cheats.add("ar", "T", "", "", "00000000 00000000\nABCD-EFGH-1234Q") + "'")
                console.log("CHEATS add patch '" + cheats.add("patch", "Test patch", "", "", "0x80001234:dword:0x00000000") + "'")
                console.log("CHEATS add AR '" + cheats.add("ar", "Test AR", "", "", "04001234 00000001") + "'")
                console.log("CHEATS add Gecko '" + cheats.add("gecko", "Test Gecko", "Tester", "line 1\nline 2", "04001234 00000002") + "'")
                const mine = cheats.items.filter(i => i.userDefined)
                console.log("CHEATS mine " + JSON.stringify(mine.map(i => [i.type, i.index, i.name, i.code, i.creator, i.notes])))
                for (const c of mine) cheats.setEnabled(c.type, c.index, true)
                const g = cheats.items.filter(i => i.userDefined && i.type === "gecko")[0]
                console.log("CHEATS edit '" + cheats.edit("gecko", g.index, "Test Gecko 2", "Tester", "", "04001234 00000003") + "'")
                const shipped = cheats.items.filter(i => !i.userDefined && i.type !== "mod")[0]
                if (shipped) console.log("CHEATS edit shipped '" + cheats.edit(shipped.type, shipped.index, "x", "", "", "0x80001234:dword:0x00000000") + "'")
                const ar = cheats.items.filter(i => i.userDefined && i.type === "ar")[0]
                cheats.remove("ar", ar.index)
                console.log("CHEATS after: " + count())
                cheats.downloadFinished.connect((d, n) => console.log("CHEATS download " + d + " added " + n + ": " + count()))
                console.log("CHEATS download start " + cheats.downloadGecko())
                stack.push(base + "CheatsPage.qml", { gameId: arg, gametdbId: tdb, gameTitle: "test" })
            }
            else if (page === "riivolution") {
                // Stage 6.4: the patches for a game (--test-arg GAMEID), the first option set to its
                // first choice, saved; then the page.
                riivolution.load(arg, 0, 0)
                console.log("RIIV sd " + riivolution.sdRoot + " xml " + riivolution.xmlFolder)
                console.log("RIIV discs " + JSON.stringify(riivolution.discs))
                if (riivolution.discs.length > 0) {
                    riivolution.setChoice(0, 0, 0, 1)
                    riivolution.save()
                    console.log("RIIV after " + JSON.stringify(riivolution.discs[0].sections[0].options[0]))
                }
                stack.push(base + "RiivolutionPage.qml", { gameId: arg, gameTitle: "test" })
            }
            else if (page === "convert") stack.push(base + "ConvertPage.qml", { path: arg })
            else if (page === "convert-run") {
                // Stage 6.4: --test-arg IN --test-arg2 "OUT FORMAT BLOCK COMPRESSION LEVEL SCRUB [CANCEL_MS]".
                const a = Qt.application.arguments
                const spec = a[a.indexOf("--test-arg2") + 1].split(" ")
                console.log("CONVERT info " + JSON.stringify(discConverter.info(arg)))
                const t0 = Date.now()
                let last = -1
                discConverter.progressChanged.connect(() => {
                    const p = Math.floor(discConverter.progress * 10)
                    if (p !== last) { last = p; console.log("CONVERT " + Math.round((Date.now() - t0) / 1000) + " s " + Math.round(discConverter.progress * 100) + "% " + discConverter.progressText) }
                })
                discConverter.finished.connect((ok, cancelled, out) => console.log("CONVERT finished ok " + ok + " cancelled " + cancelled + " after " + Math.round((Date.now() - t0) / 1000) + " s: " + out))
                console.log("CONVERT start " + discConverter.convert(arg, spec[0], Number(spec[1]), Number(spec[2]), Number(spec[3]), Number(spec[4]), spec[5] === "1"))
                if (spec.length > 6) timer2.start(Number(spec[6]))
                stack.push(base + "ConvertPage.qml", { path: arg })
            }
            else if (page === "wii-roundtrip") {
                // Stage 6.3: the Wii Menu's state, then each action's failure path with dummy files
                // (--test-arg FOLDER with bogus.wad and bogus.bin), then a disc update from GAME
                // (--test-arg2 path; a .wbfs usually has no update partition).
                wiiSystem.refresh()
                console.log("WII menu installed " + wiiSystem.menuInstalled + " version '" + wiiSystem.menuVersion + "' vWii " + wiiSystem.menuIsVWii)
                const a = Qt.application.arguments
                const disc = a.indexOf("--test-arg2") >= 0 ? a[a.indexOf("--test-arg2") + 1] : ""
                const steps = [
                    () => wiiSystem.installWad(arg + "/bogus.wad"),
                    () => wiiSystem.importSave(arg + "/bogus.bin"),
                    () => wiiSystem.importNand(arg + "/bogus.bin"),
                    () => wiiSystem.discUpdate(disc),
                ]
                let step = 0
                wiiSystem.progressChanged.connect(() => { if (wiiSystem.progressText !== "") console.log("WII progress " + wiiSystem.progress + "/" + wiiSystem.progressTotal + " " + wiiSystem.progressText.split("\n")[0]) })
                wiiSystem.finished.connect(function (task, ok, title, message) {
                    console.log("WII " + task + " " + ok + " '" + title + "' '" + message + "'")
                    if (++step < steps.length) Qt.callLater(() => console.log("WII start " + steps[step]()))
                })
                console.log("WII start " + steps[0]())
                stack.push(base + "WiiSystemPage.qml")
            }
            else if (page === "hotkeys-map") {
                // Stage 6.2: map hotkeys to a Pipes device (--test-arg DEVICE, e.g. Pipe/0/hk; the FIFO
                // User/Pipes/hk must exist before the start): Take Screenshot to Button A, Volume Up to Button B.
                testMapping = controllers.mapping("hotkeys", 0)
                testMapping.device = arg
                const groups = testMapping.groups
                for (let g = 0; g < groups.length; ++g) {
                    for (let c = 0; c < groups[g].controls.length; ++c) {
                        const name = groups[g].controls[c].name
                        if (name === "Take Screenshot") testMapping.setExpression(g, c, "`Button A`")
                        if (name === "Volume Up") testMapping.setExpression(g, c, "`Button B`")
                    }
                }
                console.log("HOTKEYS mapped " + testMapping.device + " " + JSON.stringify(testMapping.groups.slice(0, 3)))
            }
            else if (page === "log-sync") {
                // Stage 6.2: log changes reach Dolphin's LogManager, which writes its own state back at
                // exit: all types off, and the file log on, must still be so in Logger.ini afterwards.
                settingsModel.actionFinished.connect((action, ok) => console.log("LOGSYNC " + action + " " + ok))
                settingsModel.runAction("log_disable_all")
                settingsModel.setValue("LogType.BOOT", true)
                settingsModel.setValue("BooleanSetting.LOGGER_WRITE_TO_FILE", true)
                settingsModel.setValue("IntSetting.LOGGER_VERBOSITY", 4)
            }
            else if (page === "remaining-roundtrip") {
                // Stage 6.2 without a game: the dynamic lists, the log actions, the RTC, the JIT actions.
                const shaders = settingsModel.choices("postProcessingShaders", ["Off"], [""])
                console.log("R62 shaders " + shaders.entries.length + " " + JSON.stringify(shaders.entries.slice(0, 6)))
                const levels = settingsModel.choices("logVerbosity", ["Notice", "Error", "Warning", "Info", "Debug"], [1, 2, 3, 4, 5])
                console.log("R62 levels " + JSON.stringify(levels.entries))
                const log = settingsModel.menu("CONFIG_LOG").items
                console.log("R62 log items " + log.length + " last " + JSON.stringify(log[log.length - 1]))
                const rtc = "StringSetting.MAIN_CUSTOM_RTC_VALUE"
                console.log("R62 rtc before " + settingsModel.value(rtc))
                settingsModel.setValue(rtc, 1234567800)
                console.log("R62 rtc set " + settingsModel.value(rtc))
                const actions = ["log_disable_all", "log_enable_all", "log_clear", "jit_wipe_profiling", "jit_write_block_log"]
                settingsModel.actionFinished.connect(function (action, ok, message) {
                    console.log("R62 " + action + " " + ok + " '" + message + "' VI " + settingsModel.value("LogType.VI") + " SI " + settingsModel.value("LogType.SI"))
                    const next = actions.indexOf(action) + 1
                    if (next > 0 && next < actions.length) settingsModel.runAction(actions[next])
                })
                settingsModel.runAction(actions[0])
            }
        }
    }
}
