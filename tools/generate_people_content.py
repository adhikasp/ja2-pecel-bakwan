#!/usr/bin/env python3
"""Generate the native people & quest registry (src/game/Content/PeopleContent.cc).

This is the *one-off* extraction script called for by issue #156: it seeds the
compiled registry from the four layers the game's NPC and quest content used to
be split across. From then on the registry itself is the source of truth and is
edited by hand; re-running this script reproduces the same file.

The four layers (see the research comment on issue #110):

  1. Roster            assets/externalized/mercs-profile-info.json
  2. Script records    assets/externalized/script-records-NPCs.json
                       <game>/Data/Npcdata.slf + <game>/Data/NpcData/*.npc
  3. Quest ids/titles  src/game/Strategic/Quests.h, src/game/Strategic/QuestText.cc
  4. Hardcoded hooks   ~20 StartQuest/EndQuest call sites + action-code switch
                       (curated in HARDCODED below, taken from the #110 research)

Usage:
    python tools/generate_people_content.py [--game-dir DIR] [--check]

  --game-dir  install root that contains Data/ (default: the Steam Gold install)
  --check     do not write; fail if the checked-in file differs (CI / review)
"""

import argparse
import os
import re
import struct
import sys
from collections import defaultdict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_GAME_DIR = r"C:\Program Files (x86)\Steam\steamapps\common\Jagged Alliance 2 Gold"
OUT_PATH = os.path.join(REPO, "src", "game", "Content", "PeopleContent.cc")

ENTRY_BYTES = 280
NUM_RECORDS = 50
IRRELEVANT = 255
MAX_QUESTS = 30


# --------------------------------------------------------------------------- #
# Parsing helpers
# --------------------------------------------------------------------------- #

def load_jsonc(path):
    with open(path, encoding="utf-8") as f:
        s = f.read()
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"//[^\n]*", "", s)
    s = re.sub(r",\s*([}\]])", r"\1", s)
    import json
    return json.loads(s)


def parse_quest_enum(repo):
    """Return {value: 'QUEST_NAME'} for the enum Quests, in id order."""
    text = open(os.path.join(repo, "src/game/Strategic/Quests.h"), encoding="utf-8").read()
    body = re.search(r"enum Quests\s*\{(.*?)\n\};", text, re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    quests, n = {}, 0
    for part in body.split(","):
        part = part.strip()
        if not part:
            continue
        m = re.match(r"([A-Z0-9_]+)\s*(?:=\s*(\d+))?", part)
        if not m:
            continue
        if m.group(2) is not None:
            n = int(m.group(2))
        quests[n] = m.group(1)
        n += 1
    return {k: v for k, v in quests.items() if v != "NO_QUEST"}


def parse_quest_titles(repo):
    """Return [title] indexed by quest id, from QuestText.cc."""
    text = open(os.path.join(repo, "src/game/Strategic/QuestText.cc"), encoding="utf-8").read()
    body = re.search(r"QuestDescText\s*\[\s*\]\s*=\s*\{(.*?)\n\};", text, re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    return re.findall(r'"((?:[^"\\]|\\.)*)"', body)


def parse_profile_enum(repo):
    """Return {value: 'TOKEN'} for enum NPCIDs, so references stay typed."""
    text = open(os.path.join(repo, "src/game/Tactical/Soldier_Profile.h"), encoding="utf-8").read()
    body = re.search(r"enum NPCIDs\s*:\s*ProfileID\s*\{(.*?)\n\};", text, re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    result, n = {}, 0
    for part in body.split(","):
        part = part.strip()
        if not part:
            continue
        m = re.match(r"([A-Z0-9_]+)\s*(?:=\s*(\d+))?", part)
        if not m:
            continue
        if m.group(2) is not None:
            n = int(m.group(2))
        result[n] = m.group(1)
        n += 1
    return result


def read_slf(path):
    with open(path, "rb") as f:
        data = f.read()
    n = struct.unpack_from("<i", data, 512)[0]
    base = len(data) - n * ENTRY_BYTES
    files = {}
    for i in range(n):
        off = base + i * ENTRY_BYTES
        name = data[off:off + 256].split(b"\0", 1)[0].decode("ascii", "replace").lower()
        foff, flen = struct.unpack_from("<II", data, off + 256)
        state = data[off + 264]
        if state == 0:
            files[name] = data[foff:foff + flen]
    return files


def parse_records(buf):
    out = []
    for i in range(NUM_RECORDS):
        b = buf[i * 32:(i + 1) * 32]
        if len(b) < 32 or b == bytes(32):
            continue  # unused tail slot
        quest = b[8]
        start = b[15]
        end = b[16]
        out.append({"quest": quest, "start": start, "end": end})
    return out


def decode_quest(v):
    if v == IRRELEVANT:
        return None
    if 200 <= v < 200 + MAX_QUESTS:
        return ("DONE", v - 200)
    if 100 <= v < 100 + MAX_QUESTS:
        return ("NOTSTARTED", v - 100)
    if v < MAX_QUESTS:
        return ("INPROGRESS", v)
    return None


# --------------------------------------------------------------------------- #
# Hardcoded layer (curated from the #110 research comment, section 3)
# --------------------------------------------------------------------------- #
# Only quests whose start/end trigger is not carried by a script record appear
# here. `givers`/`resolvers` are the people the hardcoded trigger is centred on
# (empty means the game itself starts/ends it with no NPC contact).
HARDCODED = {
    "DELIVER_LETTER":    {"note": "started at game init by CheckForQuests; handed in to Miguel via NPC_ACTION_FATIMA_GIVE_LETTER"},
    "FOOD_ROUTE":        {"resolvers": ["FATHER"], "note": "ended by Father's action 112 through a next-day event"},
    "KILL_TERRORISTS":   {"resolvers": ["CARMEN"], "note": "ended when Carmen receives the last head"},
    "KINGPIN_MONEY":     {"note": "also started by the 'Kingpin knows the money is gone' event, ended when Kingpin dies"},
    "RESCUE_MARIA":      {"note": "EndQuest also resets Madame's brothel state"},
    "HELD_IN_ALMA":      {"note": "started by a Queen meanwhile, ended by the Alma prison event"},
    "INTERROGATION":     {"note": "started by a Queen meanwhile"},
    "FIND_SCIENTIST":    {"note": "started by the AWOL_SCIENTIST meanwhile"},
    "CREATURES":         {"note": "started by the mine event, ended by the CREATURES meanwhile"},
    "ESCORT_SKYRIDER":   {"note": "started when Skyrider is escorted"},
    "ESCORT_TOURISTS":   {"note": "started when John/Mary are escorted"},
    "FREE_CHILDREN":     {"note": "also ended when the children are freed"},
    "KILL_DEIDRANNA":    {"note": "endgame only"},
    "BLOODCATS":         {"note": "Auntie's record carries action 171 (START_BLOODCAT_QUEST); a 2-day timer fact"},
}

# Curated narrative metadata per quest (seeded, then maintained by hand).
PREREQUISITES = {
    "FOOD_ROUTE": ["DELIVER_LETTER"],  # Miguel offers it once he has the letter
}
QUEST_META = {
    "DELIVER_LETTER":  {"reward": "the Omerta rebels as allies", "rep": "", "deed": "HISTORY_ACCEPTED_ASSIGNMENT_FROM_ENRICO"},
    "FOOD_ROUTE":      {"reward": "Miguel's rebels can be hired", "rep": "", "deed": "FACT_FOOD_QUEST_OVER"},
    "KILL_TERRORISTS": {"reward": "cash for each confirmed kill", "rep": "", "deed": "FACT_ALL_TERRORISTS_KILLED"},
    "KINGPIN_IDOL":    {"reward": "Kingpin's goodwill", "rep": "San Mona", "deed": ""},
    "KINGPIN_MONEY":   {"reward": "Kingpin's goodwill", "rep": "San Mona", "deed": "FACT_KINGPIN_KNOWS_MONEY_GONE"},
    "RUNAWAY_JOEY":    {"reward": "Martha's gratitude", "rep": "", "deed": ""},
    "RESCUE_MARIA":    {"reward": "Angel's leather shop", "rep": "San Mona", "deed": "FACT_MARIA_QUEST_OVER"},
    "CHITZENA_IDOL":   {"reward": "loyalty in Chitzena", "rep": "Chitzena", "deed": ""},
    "HELD_IN_ALMA":    {"reward": "freedom", "rep": "", "deed": ""},
    "INTERROGATION":   {"reward": "escape", "rep": "", "deed": ""},
    "ARMY_FARM":       {"reward": "Keith's farm", "rep": "", "deed": ""},
    "FIND_SCIENTIST":  {"reward": "Madlab and the robot", "rep": "", "deed": ""},
    "DELIVER_VIDEO_CAMERA": {"reward": "the robot", "rep": "", "deed": ""},
    "BLOODCATS":       {"reward": "Auntie's loyalty", "rep": "", "deed": "FACT_BLOODCAT_QUEST_STARTED_TWO_DAYS_AGO"},
    "FIND_HERMIT":     {"reward": "the hermit's creature-blood vial", "rep": "", "deed": ""},
    "CREATURES":       {"reward": "the mines stay productive", "rep": "", "deed": ""},
    "CHOPPER_PILOT":   {"reward": "Skyrider's helicopter", "rep": "", "deed": ""},
    "ESCORT_SKYRIDER": {"reward": "the helicopter service", "rep": "", "deed": ""},
    "FREE_DYNAMO":     {"reward": "Dynamo can be recruited", "rep": "", "deed": ""},
    "ESCORT_TOURISTS": {"reward": "tourist gratitude", "rep": "", "deed": ""},
    "FREE_CHILDREN":   {"reward": "Doreen's change of heart", "rep": "", "deed": "FACT_DOREEN_HAD_CHANGE_OF_HEART"},
    "LEATHER_SHOP_DREAM": {"reward": "Kyle's leather shop", "rep": "", "deed": ""},
    "FREE_SHANK":      {"reward": "Shank can be recruited", "rep": "", "deed": ""},
    "KILL_DEIDRANNA":  {"reward": "Arulco is free", "rep": "", "deed": "FACT_QUEEN_DEAD"},
}


# --------------------------------------------------------------------------- #
# Extraction
# --------------------------------------------------------------------------- #

def extract(game_dir, repo):
    roster = load_jsonc(os.path.join(repo, "assets/externalized/mercs-profile-info.json"))
    quests = parse_quest_enum(repo)          # {id: 'QUEST_NAME'}
    titles = parse_quest_titles(repo)        # [title] by id
    profiles = parse_profile_enum(repo)      # {id: 'TOKEN'}
    name_by_id = {p["profileID"]: p["internalName"] for p in roster}
    id_by_name = {p["internalName"]: p["profileID"] for p in roster}

    # short names used in the JSON override ("FOOD_ROUTE") -> id
    short_to_id = {}
    for qid, token in quests.items():
        short_to_id[token.replace("QUEST_", "")] = qid

    placements = {}
    placement_path = os.path.join(repo, "assets/externalized/strategic-map-npc-placements.json")
    if os.path.exists(placement_path):
        for entry in load_jsonc(placement_path):
            placements[entry["profile"]] = {
                "sectors": entry.get("sectors", []),
                "placedAtStart": entry.get("placedAtStart", False),
            }

    # --- script records: binary, then JSON overrides (JSON fully replaces) ---
    per = defaultdict(lambda: {"cond": set(), "start": set(), "end": set()})
    if game_dir:
        slf = os.path.join(game_dir, "Data", "Npcdata.slf")
        files = read_slf(slf) if os.path.exists(slf) else {}
        loose_dir = os.path.join(game_dir, "Data", "NpcData")
        if os.path.isdir(loose_dir):
            for fn in os.listdir(loose_dir):
                if fn.lower().endswith(".npc"):
                    with open(os.path.join(loose_dir, fn), "rb") as f:
                        files[fn.lower()] = f.read()
        for fname, buf in files.items():
            if not fname.endswith(".npc"):
                continue
            stem = fname[:-4]
            if not stem.isdigit():
                continue
            pid = int(stem)
            if pid >= 200:
                continue
            for r in parse_records(buf):
                decoded = decode_quest(r["quest"])
                if decoded:
                    per[pid]["cond"].add(decoded[1])
                s, e = decode_quest(r["start"]), decode_quest(r["end"])
                if s:
                    per[pid]["start"].add(s[1])
                if e:
                    per[pid]["end"].add(e[1])

    json_records_path = os.path.join(repo, "assets/externalized/script-records-NPCs.json")
    for entry in (load_jsonc(json_records_path) if os.path.exists(json_records_path) else []):
        pid = id_by_name.get(entry["profile"])
        if pid is None:
            continue
        per[pid]["cond"].clear()
        per[pid]["start"].clear()
        per[pid]["end"].clear()
        for r in entry.get("records", []):
            if "quest" in r:
                qid = short_to_id.get(r["quest"]["name"])
                if qid is not None:
                    per[pid]["cond"].add(qid)
            if "startQuest" in r:
                qid = short_to_id.get(r["startQuest"])
                if qid is not None:
                    per[pid]["start"].add(qid)
            if "endQuest" in r:
                qid = short_to_id.get(r["endQuest"])
                if qid is not None:
                    per[pid]["end"].add(qid)

    def short(qid):
        return quests[qid].replace("QUEST_", "")

    # --- aggregate quest -> people ---
    quest_givers = defaultdict(set)
    quest_resolvers = defaultdict(set)
    quest_dialogue = defaultdict(set)
    for pid, st in per.items():
        if pid not in name_by_id:
            continue  # meanwhile/vehicle script slots (160+) are not people
        for qid in st["start"]:
            if qid in quests:
                quest_givers[qid].add(pid)
        for qid in st["end"]:
            if qid in quests:
                quest_resolvers[qid].add(pid)
        for qid in st["cond"]:
            if qid in quests:
                quest_dialogue[qid].add(pid)

    # merge the curated hardcoded layer
    for qid, token in quests.items():
        hc = HARDCODED.get(short(qid), {})
        for nm in hc.get("givers", []):
            if nm in id_by_name:
                quest_givers[qid].add(id_by_name[nm])
        for nm in hc.get("resolvers", []):
            if nm in id_by_name:
                quest_resolvers[qid].add(id_by_name[nm])

    return roster, quests, titles, profiles, placements, per, \
        quest_givers, quest_resolvers, quest_dialogue


# --------------------------------------------------------------------------- #
# Emit C++
# --------------------------------------------------------------------------- #

def ref(pid, profiles):
    """A typed ProfileID reference: the enum token if one exists."""
    return profiles.get(pid, str(pid))


def quest_token(qid, quests):
    return quests[qid]


def render(roster, quests, titles, profiles, placements, per, givers, resolvers, dialogue):
    lines = []
    w = lines.append

    w("// The native people & quest registry (issue #156), seeded once by")
    w("// tools/generate_people_content.py from the four legacy layers:")
    w("//   mercs-profile-info.json, script-records-NPCs.json (+ the binary .npc records),")
    w("//   Quests.h, QuestText.cc and strategic-map-npc-placements.json.")
    w("// This file is now the source of truth; edit it by hand. Re-running the generator")
    w("// re-seeds from the legacy layers and discards those edits (its --check mode guards it).")
    w("")
    w('#include "PeopleContent.h"')
    w('#include "Soldier_Profile.h"')
    w("")
    w("namespace People")
    w("{")
    w("")

    # ---- NPCs ----
    w("std::vector<NpcDef> const& NpcDefs()")
    w("{")
    w("\tstatic std::vector<NpcDef> const defs = {")
    for p in sorted(roster, key=lambda e: e["profileID"]):
        pid = p["profileID"]
        name = p["internalName"]
        kind = p["type"].upper()
        kind_enum = {
            "AIM": "Aim", "MERC": "Merc", "IMP": "Imp", "RPC": "Rpc",
            "NPC": "Npc", "VEHICLE": "Vehicle", "NOT_USED": "Reserved",
        }.get(kind, "Reserved")
        pl = placements.get(name, {})
        sectors = ",".join(pl.get("sectors", []))
        placed = "true" if pl.get("placedAtStart", False) else "false"
        st = per.get(pid, {"cond": set(), "start": set(), "end": set()})
        links = []
        for qid in sorted(st["start"]):
            if qid in quests:
                links.append("{%s, QuestRole::Giver}" % quest_token(qid, quests))
        for qid in sorted(st["end"]):
            if qid in quests:
                links.append("{%s, QuestRole::Resolver}" % quest_token(qid, quests))
        for qid in sorted(st["cond"]):
            if qid in quests:
                links.append("{%s, QuestRole::Dialogue}" % quest_token(qid, quests))
        link_str = ", ".join(links)
        w('\t\t{%s, NpcKind::%s, "%s", "%s", %s, 0, 0, {%s}},'
          % (ref(pid, profiles), kind_enum, name, sectors, placed, link_str))
    w("\t};")
    w("\treturn defs;")
    w("}")
    w("")

    # ---- quests ----
    w("std::vector<QuestDef> const& QuestDefs()")
    w("{")
    w("\tstatic std::vector<QuestDef> const defs = {")
    token_by_short = {v.replace("QUEST_", ""): v for v in quests.values()}
    for qid in sorted(quests):
        token = quests[qid]
        short = token.replace("QUEST_", "")
        title = titles[qid] if qid < len(titles) else ""
        gv = sorted(givers.get(qid, set()))
        rs = sorted(resolvers.get(qid, set()))
        dl = sorted(dialogue.get(qid, set()))
        self_resolving = "true" if (not gv or not rs) else "false"
        meta = QUEST_META.get(short, {})
        hc = HARDCODED.get(short, {})
        note = hc.get("note", "")
        gv_str = ", ".join(ref(x, profiles) for x in gv)
        rs_str = ", ".join(ref(x, profiles) for x in rs)
        dl_str = ", ".join(ref(x, profiles) for x in dl)
        prereq_str = ", ".join(token_by_short[n] for n in PREREQUISITES.get(short, []) if n in token_by_short)
        w('\t\t{%s, "%s", "%s", {%s}, {%s}, %s, {%s}, {%s}, "%s", "%s", "%s", "%s"},'
          % (token, short, title, gv_str, rs_str, self_resolving, dl_str, prereq_str,
             meta.get("reward", ""), meta.get("rep", ""), meta.get("deed", ""), note))
    w("\t};")
    w("\treturn defs;")
    w("}")
    w("")

    w("NpcDef const* FindNpc(ProfileID id)")
    w("{")
    w("\tfor (NpcDef const& def : NpcDefs()) if (def.id == id) return &def;")
    w("\treturn nullptr;")
    w("}")
    w("")
    w("QuestDef const* FindQuest(Quests id)")
    w("{")
    w("\tfor (QuestDef const& def : QuestDefs()) if (def.id == id) return &def;")
    w("\treturn nullptr;")
    w("}")
    w("")
    w("const char* NpcName(ProfileID id)")
    w("{")
    w("\tNpcDef const* const def = FindNpc(id);")
    w('\treturn def ? def->name : "";')
    w("}")
    w("")
    w("const char* QuestTitle(Quests id)")
    w("{")
    w("\tQuestDef const* const def = FindQuest(id);")
    w('\treturn def ? def->title : "";')
    w("}")
    w("")
    w("const char* NpcKindName(NpcKind kind)")
    w("{")
    w("\tswitch (kind)")
    w("\t{")
    w('\t\tcase NpcKind::Aim:      return "AIM";')
    w('\t\tcase NpcKind::Merc:     return "MERC";')
    w('\t\tcase NpcKind::Imp:      return "IMP";')
    w('\t\tcase NpcKind::Rpc:      return "RPC";')
    w('\t\tcase NpcKind::Npc:      return "NPC";')
    w('\t\tcase NpcKind::Vehicle:  return "VEHICLE";')
    w('\t\tcase NpcKind::Reserved: return "RESERVED";')
    w("\t}")
    w('\treturn "";')
    w("}")
    w("")
    w("const char* QuestRoleName(QuestRole role)")
    w("{")
    w("\tswitch (role)")
    w("\t{")
    w('\t\tcase QuestRole::Giver:    return "giver";')
    w('\t\tcase QuestRole::Resolver: return "resolver";')
    w('\t\tcase QuestRole::Dialogue: return "dialogue";')
    w("\t}")
    w('\treturn "";')
    w("}")
    w("")
    w("const char* QuestStageName(QuestStage stage)")
    w("{")
    w("\tswitch (stage)")
    w("\t{")
    w('\t\tcase QuestStage::NotStarted: return "NOT_STARTED";')
    w('\t\tcase QuestStage::InProgress: return "IN_PROGRESS";')
    w('\t\tcase QuestStage::Done:       return "DONE";')
    w("\t}")
    w('\treturn "";')
    w("}")
    w("")
    w("void ApplyQuestTransition(QuestTransition const& transition, const SGPSector& sector)")
    w("{")
    w("\tif (transition.change == QuestChange::Start)")
    w("\t{")
    w("\t\tStartQuest(static_cast<UINT8>(transition.quest), sector);")
    w("\t}")
    w("\telse")
    w("\t{")
    w("\t\tEndQuest(static_cast<UINT8>(transition.quest), sector);")
    w("\t}")
    w("}")
    w("")
    w("void ApplyQuestTransition(uint8_t rawQuest, QuestChange change, const SGPSector& sector)")
    w("{")
    w("\tif (rawQuest == NO_QUEST) return;")
    w("\tApplyQuestTransition({ static_cast<Quests>(rawQuest), change }, sector);")
    w("}")
    w("")
    w("bool IsReservedProfile(ProfileID id)")
    w("{")
    w("\tNpcDef const* const def = FindNpc(id);")
    w("\treturn def != nullptr && def->kind == NpcKind::Reserved;")
    w("}")
    w("")
    w("} // namespace People")
    w("")
    return "\n".join(lines)


# --------------------------------------------------------------------------- #
# Main
# --------------------------------------------------------------------------- #

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game-dir", default=os.environ.get("JA2_GAME_DIR", DEFAULT_GAME_DIR))
    ap.add_argument("--repo", default=REPO)
    ap.add_argument("--out", default=OUT_PATH)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    game_dir = args.game_dir if args.game_dir and os.path.isdir(args.game_dir) else None
    if not game_dir:
        if args.check:
            print("error: --check needs the game data (--game-dir) for the binary .npc records", file=sys.stderr)
            return 2
        print("warning: no game dir; binary .npc records are not included", file=sys.stderr)

    data = extract(game_dir, args.repo)
    text = render(*data)

    if args.check:
        if not os.path.exists(args.out):
            print("error: {} does not exist".format(args.out), file=sys.stderr)
            return 1
        with open(args.out, encoding="utf-8") as f:
            if f.read() != text:
                print("error: {} is out of date; re-run the generator".format(args.out), file=sys.stderr)
                return 1
        print("{} is up to date".format(args.out))
        return 0

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote {} ({} bytes)".format(args.out, len(text)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
