#!/usr/bin/env python3
# Generates Source/Core/DolphinUT/settings.json from the Android app's settings (stage 3.6,
# docs/ut-port/stage3.6-settings.md), so the port's settings follow the Android app's menus, labels,
# choices and ranges, with the Android sources as the one source:
#   - features/settings/ui/SettingsFragmentPresenter.kt: the menus (add*Settings) and their items;
#   - features/settings/model/{Boolean,Int,Float,String}Setting.kt: each setting's file, section,
#     key and default;
#   - res/values/strings.xml and arrays.xml: titles, descriptions and choice lists.
# Items the generic settings page cannot show (actions, file pickers, controller pages, ...) are
# listed under "skipped" for the notes.
# It also writes SettingsTable.inc, which binds each setting to Dolphin's own Config::Info object of
# the same name (Core/Config/*.cpp) when Dolphin has it at the same location, so the port reads and
# writes real values with Dolphin's defaults (the Android app's defaults are Android's: dual core is
# on there, off on desktop Linux). Settings Dolphin does not have keep the Android default.
# Usage: ut/tools/gen-settings.py   (writes Source/Core/DolphinUT/settings.json and SettingsTable.inc)
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = root / "Source/Android/app/src/main"
settings_dir = main / "java/org/dolphinemu/dolphinemu/features/settings"


def android_text(text):
    if text is None:
        return ""
    text = text.strip()
    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        text = text[1:-1]
    return (text.replace("\\'", "'").replace('\\"', '"').replace("\\n", "\n")
            .replace("\\u2026", "…"))


strings = {}
for e in ET.parse(main / "res/values/strings.xml").getroot():
    if e.tag == "string":
        strings[e.get("name")] = android_text("".join(e.itertext()))

arrays = {}
for e in ET.parse(main / "res/values/arrays.xml").getroot():
    if e.tag in ("string-array", "integer-array", "array"):
        items = []
        for item in e:
            text = (item.text or "").strip()
            if text.startswith("@string/"):
                text = strings.get(text[8:], text)
            items.append(android_text(text))
        arrays[e.get("name")] = items

# Settings.FILE_* and SECTION_* constants.
constants = dict(re.findall(r'const val ((?:FILE|SECTION)_\w+) = "([^"]*)"',
                            (settings_dir / "model/Settings.kt").read_text()))


def literal(value):
    value = value.strip()
    if value in ("true", "false"):
        return value == "true"
    m = re.fullmatch(r"-?\d+(\.\d+)?f?", value)
    if m:
        return float(value.rstrip("f")) if ("." in value or value.endswith("f")) else int(value)
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        return value[1:-1]
    return None


definitions = {}
for kind in ("Boolean", "Int", "Float", "String"):
    text = (settings_dir / f"model/{kind}Setting.kt").read_text()
    for m in re.finditer(r"\n    (\w+)\(\s*Settings\.(FILE_\w+),\s*(Settings\.(SECTION_\w+)|\"([^\"]*)\"),"
                         r"\s*\"([^\"]*)\",\s*(.*?)\s*\)(?:,|;)(?=\s*\n)", text, re.S):
        name, file_const, _, section_const, section_literal, key, default = m.groups()
        section = constants.get(section_const, "") if section_const else section_literal
        value = literal(default)
        definitions[f"{kind}Setting.{name}"] = {
            "type": kind.lower(),
            "file": constants[file_const],
            "section": section,
            "key": key,
            # A default the generator cannot evaluate (an expression) is "dynamic": the port takes
            # it from Dolphin (SettingsModel).
            "default": value,
            "dynamic": value is None,
            "expression": default.strip() if value is None else None,
        }


def balanced(text, start):
    """The text between text[start] == '(' and its matching ')'."""
    depth = 0
    i = start
    in_string = False
    while i < len(text):
        c = text[i]
        if in_string:
            if c == "\\":
                i += 1
            elif c == '"':
                in_string = False
        elif c == '"':
            in_string = True
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                return text[start + 1:i], i
        i += 1
    raise ValueError("unbalanced")


def split_args(text):
    args, depth, current, in_string = [], 0, "", False
    for c in text:
        if in_string:
            current += c
            if c == '"':
                in_string = False
            continue
        if c == '"':
            in_string = True
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == "," and depth == 0:
            args.append(current.strip())
            current = ""
        else:
            current += c
    if current.strip():
        args.append(current.strip())
    return args


def res_string(arg):
    m = re.fullmatch(r"R\.string\.(\w+)", arg.strip())
    return strings.get(m.group(1), m.group(1)) if m else ""


def res_array(arg):
    m = re.fullmatch(r"R\.array\.(\w+)", arg.strip())
    return arrays.get(m.group(1)) if m else None


def number(arg):
    value = literal(arg)
    if value is None:
        # if (condition) a else b: the usual case, b.
        m = re.search(r"else\s+(-?[\d.]+f?)\s*$", arg.strip())
        value = literal(m.group(1)) if m else None
    return value


# The file filters of the Android app's pickers (SettingsActivityResultLaunchers, FileBrowserHelper).
PICKER_EXTENSIONS = {
    "requestGameFile": ["gcm", "tgc", "bin", "iso", "ciso", "gcz", "wbfs", "wia", "rvz", "nfs",
                        "wad", "dol", "elf", "json"],
    "requestGbaRomFile": ["gba", "gbc", "gb", "agb", "mb", "rom", "bin"],
    "requestBinFile": ["bin"],
    "requestRawFile": ["raw"],
    "requestDirectory": [],
}
# Actions the port implements (SettingsModel::runAction), or the page it opens instead.
RUNNABLE_ACTIONS = {
    "UserDataActivity.launch": "UserDataPage.qml",
    "syncSdFolderToSdImage": "sd_pack",
    "syncSdImageToSdFolder": "sd_unpack",
}

presenter = (settings_dir / "ui/SettingsFragmentPresenter.kt").read_text()
tags = dict(re.findall(r"MenuTag\.(\w+) -> (add\w+Settings)\(sl\)", presenter))

menus = {}
skipped = []
for tag, function in tags.items():
    m = re.search(r"private fun " + function + r"\(sl: ArrayList<SettingsItem>\) \{", presenter)
    if not m:
        continue
    _, end = balanced(presenter, presenter.index("(", m.start()))
    body_start = presenter.index("{", end)
    depth, i = 0, body_start
    while True:
        if presenter[i] == "{":
            depth += 1
        elif presenter[i] == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    body = presenter[body_start:i]

    items = []
    for add in re.finditer(r"sl\.add\(\s*(\w+)\(", body):
        kind = add.group(1)
        args_text, args_end = balanced(body, body.index("(", add.end() - 1))
        args = split_args(args_text)
        # RunRunnable's action is a trailing lambda: `RunRunnable(...) { ... })`.
        trailing = body[args_end + 1:body.find("\n        sl.add", args_end)]
        setting = next((a for a in args if re.fullmatch(r"(Boolean|Int|Float|String)Setting\.\w+", a)),
                       None)
        item = None
        if kind in ("SwitchSetting", "InvertedSwitchSetting") and setting and len(args) >= 4:
            item = {"type": "switch", "setting": setting, "title": res_string(args[2]),
                    "description": res_string(args[3]), "inverted": kind == "InvertedSwitchSetting"}
        elif kind in ("SingleChoiceSetting", "SingleChoiceSettingDynamicDescriptions") and setting:
            entries, values = res_array(args[4]), res_array(args[5]) if len(args) > 5 else None
            if entries:
                item = {"type": "choice", "setting": setting, "title": res_string(args[2]),
                        "description": res_string(args[3]), "entries": entries,
                        "values": [literal(v) if literal(v) is not None else v for v in values]
                        if values else list(range(len(entries)))}
        elif kind == "StringSingleChoiceSetting" and setting:
            entries = res_array(args[4]) if len(args) > 4 else None
            values = res_array(args[5]) if len(args) > 5 else None
            if entries:
                item = {"type": "choice", "setting": setting, "title": res_string(args[2]),
                        "description": res_string(args[3]), "entries": entries,
                        "values": values or entries}
        elif kind in ("IntSliderSetting", "FloatSliderSetting", "PercentSliderSetting") and setting:
            item = {"type": "slider", "setting": setting, "title": res_string(args[2]),
                    "description": res_string(args[3]), "min": number(args[4]),
                    "max": number(args[5]), "units": literal(args[6]) or "",
                    "step": number(args[7]) if len(args) > 7 else 1,
                    "percent": kind == "PercentSliderSetting"}
        elif kind == "InputStringSetting" and setting:
            item = {"type": "text", "setting": setting, "title": res_string(args[2]),
                    "description": res_string(args[3])}
        elif kind == "SubmenuSetting" and len(args) >= 3:
            target = re.fullmatch(r"MenuTag\.(\w+)", args[2])
            if target:
                item = {"type": "submenu", "title": res_string(args[1]), "menu": target.group(1)}
        elif kind == "HeaderSetting" and len(args) >= 2:
            item = {"type": "header", "title": res_string(args[1])}
        elif kind in ("FilePicker", "DirectoryPicker") and setting and len(args) >= 5:
            # Paths (stage 3.7): the port's file browser; global only, as paths do not belong to a game.
            launcher = args[4].rsplit(".", 1)[-1]
            item = {"type": "folder" if kind == "DirectoryPicker" else "file", "setting": setting,
                    "title": res_string(args[2]), "description": res_string(args[3]),
                    "extensions": PICKER_EXTENSIONS.get(launcher, []), "globalOnly": True}
        elif kind == "RunRunnable" and len(args) >= 4:
            action = next((a for key, a in RUNNABLE_ACTIONS.items() if key in trailing), None)
            if action and action.endswith(".qml"):
                item = {"type": "page", "title": res_string(args[1]), "page": action,
                        "globalOnly": True}
            elif action:
                item = {"type": "action", "action": action, "title": res_string(args[1]),
                        "description": res_string(args[2]), "confirmation": res_string(args[3]),
                        "globalOnly": True}
        if item is None:
            skipped.append({"menu": tag, "kind": kind,
                            "title": next((res_string(a) for a in args if a.startswith("R.string.")), "")})
            continue
        if "setting" in item and item["setting"] not in definitions:
            skipped.append({"menu": tag, "kind": kind, "title": item["title"],
                            "reason": "setting not found: " + item["setting"]})
            continue
        items.append(item)
    menus[tag] = {"items": items}

# The port's adjustments (docs/ut-port/stage3.6-settings.md):
# - Android-only items are left out;
# - the controller menus open the port's Controllers page (stage 3.5);
# - items Android builds from converter objects become the port's virtual settings (SettingsModel);
# - the port's own CPU boost goes after Dual Core.
HIDDEN_SETTINGS = {"IntSetting.MAIN_EMULATION_ORIENTATION", "BooleanSetting.MAIN_EXPAND_TO_CUTOUT_AREA",
                   # Analytics are not built into the port (ENABLE_ANALYTICS=OFF).
                   "BooleanSetting.MAIN_ANALYTICS_ENABLED"}
HIDDEN_MENUS = {"GPU_DRIVERS", "HOTKEYS", "CONFIG_ACHIEVEMENTS"}
REDIRECTS = {"GCPAD_TYPE": "ControllersPage.qml", "WIIMOTE": "ControllersPage.qml"}
# The top level's "Long press a setting to clear it." is shown with game settings only, as there.
for item in menus["SETTINGS"]["items"]:
    if item["type"] == "header" and item["title"] == strings.get("setting_clear_info"):
        item["gameOnly"] = True
for menu in menus.values():
    kept = []
    for item in menu["items"]:
        if item.get("setting") in HIDDEN_SETTINGS or item.get("menu") in HIDDEN_MENUS:
            continue
        if item["type"] == "submenu" and item["menu"] in REDIRECTS:
            # The Controllers page edits the global setup; per-game controller profiles are not
            # done yet, so it is left out of game settings.
            item = {"type": "page", "title": item["title"], "page": REDIRECTS[item["menu"]],
                    "globalOnly": True}
        kept.append(item)
    menu["items"] = kept
for tag in HIDDEN_MENUS | set(REDIRECTS):
    menus.pop(tag, None)


def insert_after(menu, predicate, new_item):
    items = menus[menu]["items"]
    index = next((i for i, item in enumerate(items) if predicate(item)), len(items) - 1)
    items.insert(index + 1, new_item)


def virtual(name, kind, default):
    definitions[name] = {"type": kind, "file": "", "section": "", "key": "", "default": default,
                         "dynamic": False, "expression": None, "virtual": True}


virtual("Virtual.CPU_BOOST", "int", 1024)
insert_after("CONFIG_GENERAL", lambda i: i.get("setting") == "BooleanSetting.MAIN_CPU_THREAD", {
    "type": "choice", "setting": "Virtual.CPU_BOOST", "title": "CPU Boost",
    "description": "Keeps the CPU clocked up while a game runs (Ubuntu Touch has no game mode). "
                   "Maximum doubled the speed of demanding games in testing.",
    "entries": ["Off", "Medium", "Maximum"], "values": [0, 512, 1024]})
virtual("Virtual.DSP_ENGINE", "int", 0)
menus["CONFIG_AUDIO"]["items"].insert(0, {
    "type": "choice", "setting": "Virtual.DSP_ENGINE", "title": strings["dsp_emulation_engine"],
    "description": "", "entries": arrays["dspEngineEntriesGeneric"], "values": [0, 2]})
insert_after("CONFIG_ADVANCED", lambda i: i["type"] == "header" and i["title"] == strings["cpu_options"], {
    "type": "choice", "setting": "IntSetting.MAIN_CPU_CORE", "title": strings["cpu_core"],
    "description": "", "entries": arrays["emuCoresEntriesARM64"], "values": [4, 5, 0]})
virtual("Virtual.SYNC_GPU", "int", 1)
insert_after("CONFIG_ADVANCED", lambda i: i["type"] == "header" and i["title"] == strings["gpu_options"], {
    "type": "choice", "setting": "Virtual.SYNC_GPU", "title": strings["synchronize_gpu_thread"],
    "description": strings["synchronize_gpu_thread_description"],
    "entries": arrays["synchronizeGpuThreadEntries"], "values": [0, 1, 2]})
virtual("Virtual.MEM1_MB", "int", 24)
virtual("Virtual.MEM2_MB", "int", 64)
insert_after("CONFIG_ADVANCED", lambda i: i.get("setting") == "BooleanSetting.MAIN_RAM_OVERRIDE_ENABLE", {
    "type": "slider", "setting": "Virtual.MEM2_MB", "title": strings["main_mem2_size"],
    "description": "", "min": 64, "max": 256, "units": "MB", "step": 1, "percent": False})
insert_after("CONFIG_ADVANCED", lambda i: i.get("setting") == "BooleanSetting.MAIN_RAM_OVERRIDE_ENABLE", {
    "type": "slider", "setting": "Virtual.MEM1_MB", "title": strings["main_mem1_size"],
    "description": "", "min": 24, "max": 64, "units": "MB", "step": 1, "percent": False})
skipped = [s for s in skipped if s["title"] not in (
    strings["dsp_emulation_engine"], strings["cpu_core"], strings["synchronize_gpu_thread"],
    strings["main_mem1_size"], strings["main_mem2_size"])]
if "IntSetting.MAIN_CPU_CORE" not in definitions:
    sys.exit("MAIN_CPU_CORE not found")

# Menu titles: the submenu entries pointing at them.
titles = {"SETTINGS": "Settings"}
for menu in menus.values():
    for item in menu["items"]:
        if item["type"] == "submenu":
            titles.setdefault(item["menu"], item["title"])
for tag, menu in menus.items():
    menu["title"] = titles.get(tag, tag.replace("_", " ").title())

used = {item["setting"] for menu in menus.values() for item in menu["items"] if "setting" in item}

# Dolphin's own settings: name -> (type, system, section, key).
core = {}
for path in sorted((root / "Source/Core/Core/Config").glob("*.cpp")):
    text = path.read_text()
    for m in re.finditer(r"const Info<([^>]+)> (\w+)\{\s*\{\s*System::(\w+),\s*\"([^\"]*)\",\s*\"([^\"]*)\"\s*\}",
                         text):
        core[m.group(2)] = (m.group(1), m.group(3), m.group(4), m.group(5))
FILES = {"Main": "Dolphin", "GFX": "GFX", "SYSCONF": "SYSCONF", "Logger": "Logger",
         "Achievements": "RetroAchievements", "WiiPad": "WiimoteNew"}
SYSTEMS = {v: k for k, v in FILES.items()}

table = []
for name in sorted(used):
    d = definitions[name]
    if d.get("virtual"):
        continue
    short = name.split(".", 1)[1]
    c = core.get(short)
    if c:
        # Dolphin's object wins: it is what the emulator reads.
        if not (FILES.get(c[1]) == d["file"] and c[2] == d["section"] and c[3] == d["key"]):
            print(f"{name}: the Android app uses {d['file']}/{d['section']}/{d['key']}, Dolphin "
                  f"{c[1]}/{c[2]}/{c[3]}; using Dolphin's", file=sys.stderr)
        table.append(f'    {{"{name}", Bind(Config::{short})}},')
        d["core"] = True
        continue
    cpp_type = {"boolean": "bool", "int": "int", "float": "float", "string": "std::string"}[d["type"]]
    default = d["default"]
    if d["dynamic"]:
        default = {"bool": False, "int": 0, "float": 0.0, "std::string": ""}[cpp_type]
        expression = d["expression"] or ""
        if expression.startswith("0x"):
            default = int(expression, 16)
    literal_default = ("true" if default else "false") if cpp_type == "bool" else \
        (json.dumps(default) if cpp_type == "std::string" else
         (f"{float(default)}f" if cpp_type == "float" else str(int(default))))
    table.append(f'    {{"{name}", Bind(std::make_shared<Config::Info<{cpp_type}>>(Config::Location{{'
                 f'Config::System::{SYSTEMS[d["file"]]}, "{d["section"]}", "{d["key"]}"}}, {literal_default}))}},')
    d["core"] = False

out_dir = root / "Source/Core/DolphinUT"
(out_dir / "SettingsTable.inc").write_text(
    "// Generated by ut/tools/gen-settings.py from the Android app's settings and Dolphin's\n"
    "// Core/Config. Do not edit. Name -> Dolphin's Config::Info (or the Android app's definition).\n"
    + "\n".join(table) + "\n")

with open(out_dir / "settings.json", "w") as out:
  json.dump({
    "generated_from": "Source/Android (ut/tools/gen-settings.py)",
    "menus": menus,
    "settings": {name: definitions[name] for name in sorted(used)},
    "skipped": skipped,
  }, out, indent=1, ensure_ascii=False)
  out.write("\n")
print(f"{sum(len(m['items']) for m in menus.values())} items in {len(menus)} menus, "
      f"{len(used)} settings ({sum(1 for n in used if definitions[n].get('core'))} bound to Dolphin's), "
      f"{len(skipped)} skipped")
