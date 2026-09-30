#!/usr/bin/env python3
"""Writes the Phase 4 wireframes of the native strategic map screen (M2, docs/ui/mapscreen.md) to
assets/ui/mocks/phase4/*.rml. The map is drawn by the UI from sector data (towns, mines, SAM sites from
assets/externalized, plus the literal campaign state of the mock) over the original map art, which the game
provides at runtime as the image "strategic-map". Run it after changing the mock data or layout:

    python tools/ui/phase4_mocks.py

and look at the result in the game: ja2.debug("mock", "phase4/overview") (see tests/e2e/manual/phase4_mocks.lua).
"""
import json
import os
import re

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "assets", "ui", "mocks", "phase4")
EXT = os.path.join(ROOT, "assets", "externalized")
SW, SH = 64, 55  # a sector in dp
ROWS = "ABCDEFGHIJKLMNOP"


def load(name):
    with open(os.path.join(EXT, name), encoding="utf8") as f:
        t = re.sub(r"/\*.*?\*/", "", f.read(), flags=re.S)
    return json.loads(re.sub(r"^\s*//.*$", "", t, flags=re.M))


def sec(code):
    """"B13" -> (13, 2)"""
    return int(code[1:]), ROWS.index(code[0]) + 1


def px(x, y):
    return (x - 1) * SW, (y - 1) * SH


def centre(code):
    x, y = sec(code)
    return (x - 1) * SW + SW // 2, (y - 1) * SH + SH // 2


TOWNS = {t["internalName"]: [sec(s) for s in t["sectors"]] for t in load("strategic-map-towns.json")}
TOWN_NAMES = {"OMERTA": "Omerta", "DRASSEN": "Drassen", "ALMA": "Alma", "GRUMM": "Grumm", "TIXA": "Tixa",
              "CAMBRIA": "Cambria", "SAN_MONA": "San Mona", "ESTONI": "Estoni", "ORTA": "Orta", "BALIME": "Balime",
              "MEDUNA": "Meduna", "CHITZENA": "Chitzena"}
with open(os.path.join(EXT, "strategic-mines.json"), encoding="utf8") as _f:
    MINES = [sec(c) for c in re.findall(r'"entranceSector"\s*:\s*"([A-P]\d+)"', _f.read())]

# ------------------------------------------------------------------ the campaign of the mock (Day 12, Drassen taken)
def rng(row, a, b):
    return {(x, ROWS.index(row) + 1) for x in range(a, b + 1)}


VISITED = rng("A", 8, 13) | rng("B", 8, 14) | rng("C", 10, 14) | rng("D", 11, 14) | rng("E", 12, 14)
OURS = {sec(s) for s in ["A9", "A10", "A11", "B10", "B11", "B12", "B13", "C12", "C13", "D13"]}
ENEMY_KNOWN = {sec(s) for s in ["C11", "D14", "E13", "B14"]}
LOYALTY = {"OMERTA": 82, "DRASSEN": 64}
TOWN_SIDE = {"OMERTA": "ours", "DRASSEN": "ours"}
MILITIA = {"B13": 4, "C13": 3, "D13": 2}
ENEMIES = {"C11": "5", "D14": "3", "E13": "?", "B14": "2"}
TEAMS = {"B13": 5, "C13": 4}
SAMS = {"D15": "enemy"}

MERCS = [  # face, nick, full, assignment icon, assignment, loc, dest, contract, status, hp, en, morale, group
    (7, "Ivan", "Ivan Dolvich", "squad", "Squad 1", "B13", "", "5.3d", "", 82, 64, 70, 1),
    (3, "Grizzly", "Steve Bornell", "squad", "Squad 1", "B13", "", "5.3d", "", 95, 58, 66, 1),
    (2, "Lynx", "Kyle Simmons", "squad", "Squad 1", "B13", "", "2.1d", "", 60, 71, 52, 1),
    (13, "Fidel", "Fidel Dahan", "squad", "Squad 2", "C13", "D14 &#183; 1:20", "6.0d", "", 100, 80, 75, 2),
    (17, "Buns", "Monica Sondergaard", "squad", "Squad 2", "C13", "D14 &#183; 1:20", "6.0d", "", 88, 77, 81, 2),
    (8, "Steroid", "Kamil Janick", "squad", "Squad 2", "C13", "D14 &#183; 1:20", "13h", "warn", 91, 69, 60, 2),
    (4, "Vicki", "Vicki Waters", "doctor", "Doctor", "B13", "", "3.4d", "", 100, 90, 72, 0),
    (0, "Barry", "Barry Unger", "train-town", "Train militia", "C13", "", "4.0d", "asleep", 74, 22, 64, 0),
    (36, "Scope", "Sheila Sterling", "in-transit", "In transit", "&#8212;", "B13 &#183; 18:00", "7.0d", "", 100, 100, 70, 3),
]


def e(s):
    return s.replace("&", "&amp;").replace("&amp;#", "&#")


# ------------------------------------------------------------------ top bar
def topbar(filters=("towns", "teams"), paused=False, laptop_badge=2, tc="5m"):
    f = [("towns", "town", "Towns", "W"), ("mines", "mine", "Mines", "M"), ("teams", "player-group", "Teams", "T"),
         ("militia", "militia", "Militia", "Z"), ("airspace", "helicopter", "Airspace", "A"),
         ("items", "sector-inventory", "Items", "I")]
    chips = "".join(
        f'<div class="flt{" first" if i == 0 else ""}{" on" if k in filters else ""}" id="map.filter.{k}" title="Show {l} ({kb})">'
        f'<img class="icon" src="icon-{ic}"/><span>{l}</span><span class="kbd">{kb}</span></div>'
        for i, (k, ic, l, kb) in enumerate(f))
    tcs = [("pause", '<img class="icon" src="icon-pause"/>'), ("5m", "<span>5m</span>"), ("30m", "<span>30m</span>"),
           ("1h", "<span>1h</span>"), ("6h", "<span>6h</span>")]
    act = "pause" if paused else tc
    tcb = "".join(f'<div class="tc-btn{" active" if k == act else ""}" id="map.time.{k}">{h}</div>' for k, h in tcs)
    badge = f'<span class="badge info">{laptop_badge}</span>' if laptop_badge else ""
    return f"""
	<div class="p4-top">
		<div class="timectl{" paused" if paused else ""}" id="map.time" title="Time compression: + and -, Space starts and stops">
			<div class="clock"><span class="day">DAY 12</span>14:35</div>{tcb}
		</div>
		<div class="filters" id="map.filters">{chips}</div>
		<div class="grow"></div>
		<div class="money" id="map.balance" title="Current balance and daily income"><span class="k">Balance &#183; income</span><span class="v">$48,250 <span class="inc">+$3,450/d</span></span></div>
		<button class="btn" id="map.laptop"><img class="icon" src="icon-laptop"/><span class="label">Laptop</span>{badge}<span class="kbd">L</span></button>
		<button class="btn" id="map.tactical"><img class="icon" src="icon-exit-sector"/><span class="label">Tactical</span><span class="kbd">Esc</span></button>
		<button class="icon-btn" id="map.options" title="Options (O)"><img class="icon" src="icon-options"/></button>
		<button class="icon-btn" id="map.menu" title="Save, load, help, game settings"><img class="icon" src="icon-menu"/></button>
	</div>"""


# ------------------------------------------------------------------ team dock
def team_rows(selected=("Ivan",), plotting=(), multi=(), hover=None, grouped=True):
    groups = {1: ("squad", "Squad 1", "B13 &#183; Drassen"), 2: ("merc-moving", "Squad 2", "C13 &#8594; D14 &#183; arrives 15:55"),
              0: ("on-duty", "Not in a squad", ""), 3: ("in-transit", "Arriving", "Drassen airport")}
    out = []
    order = [1, 2, 0, 3]
    for g in order:
        if grouped:
            ic, n, r = groups[g]
            out.append(f'<div class="grp"><img class="icon" src="icon-{ic}"/><span>{n}</span><span class="r">{r}</span></div>')
        for i, m in enumerate(MERCS):
            face, nick, full, aic, asg, loc, dest, con, st, hp, en, mo, grp = m
            if grp != g:
                continue
            cls = "tr"
            if nick in selected:
                cls += " selected"
            if nick in plotting:
                cls += " plotting"
            if nick in multi:
                cls += " multi"
            if nick == hover:
                cls += " is-hover"
            if grp == 3:
                cls += " dimmed"
            sti = ('<img class="icon" src="icon-asleep" title="Asleep"/>' if st == "asleep" else "")
            conc = "c-co warn-t" if st == "warn" else "c-co"
            coni = f'{con}' if st != "warn" else f'{con}'
            hpbars = (f'<div class="c-hp wide-only"><div class="bar hp"><div class="fill" style="width: {hp}%"></div></div>'
                      f'<div class="bar breath"><div class="fill" style="width: {en}%"></div></div>'
                      f'<div class="bar morale"><div class="fill" style="width: {mo}%"></div></div></div>')
            out.append(
                f'<div class="{cls}" id="map.team[{i}]"><div class="c-st">{sti}</div>'
                f'<div class="c-nm"><span class="strong">{nick}</span></div>'
                f'<div class="c-as" id="map.team[{i}].assignment"><img class="icon" src="icon-{aic}"/><span>{asg}</span></div>'
                f'<span class="c-lo" id="map.team[{i}].location">{loc}</span>'
                f'<span class="c-de" id="map.team[{i}].destination">{dest or "&#8212;"}</span>'
                f'{hpbars}<span class="{conc}" id="map.team[{i}].contract">{coni}</span></div>')
    # the helicopter is a vehicle row too
    out.append('<div class="grp"><img class="icon" src="icon-vehicle"/><span>Vehicles</span><span class="r"></span></div>')
    out.append('<div class="tr" id="map.team[9]"><div class="c-st"></div><div class="c-nm"><span class="strong">Helicopter</span></div>'
               '<div class="c-as"><img class="icon" src="icon-helicopter"/><span>Skyrider</span></div><span class="c-lo">B13</span>'
               '<span class="c-de">&#8212;</span><div class="c-hp wide-only"><div class="bar ok"><div class="fill" style="width: 100%"></div></div>'
               '<div class="bar breath"><div class="fill" style="width: 72%"></div></div><div class="bar disabled"><div class="fill" style="width: 0%"></div></div></div>'
               '<span class="c-co">&#8212;</span></div>')
    return "".join(out)


def team_dock(detail=True, **kw):
    head = """
		<div class="dock-head"><span class="t">Team</span><span class="m">9 mercs &#183; 1 vehicle</span>
			<button class="icon-btn on" id="map.team.group" title="Group by squad"><img class="icon" src="icon-squad"/></button>
			<button class="icon-btn" id="map.team.filter" title="Filter"><img class="icon" src="icon-filter"/></button>
			<button class="icon-btn" id="map.team.collapse" title="Hide the team panel"><img class="icon" src="icon-chevron-left"/></button>
		</div>"""
    thead = ('<div class="thead"><span class="th c-st"></span>'
             '<span class="th c-nm sortable sorted" id="map.sort.name"><span>Name</span><img class="icon" src="icon-sort-asc"/><span class="kbd">F1</span></span>'
             '<span class="th c-as sortable" id="map.sort.assignment"><span>Assignment</span><span class="kbd">F2</span></span>'
             '<span class="th c-lo sortable" id="map.sort.location"><span>Loc</span><span class="kbd">F4</span></span>'
             '<span class="th c-de sortable" id="map.sort.destination" title="Sort by destination (F5)"><span>Destination</span></span>'
             '<span class="th c-hp wide-only"><span>Health &#183; energy &#183; morale</span></span>'
             '<span class="th c-co sortable" id="map.sort.contract" title="Sort by contract left (F6)"><span>Left</span></span></div>')
    foot = ('<div class="team-foot"><span class="kbd">1</span><span>-</span><span class="kbd">0</span><span>select squad</span>'
            '<span class="kbd">Ctrl</span><span>add</span><span class="kbd">Shift</span><span>range</span>'
            '<span class="kbd">&#8592;</span><span class="kbd">&#8594;</span><span>next merc</span></div>')
    return f"""
	<div class="dock dock-l" id="map.teamdock">{head}
		<div class="team table scroll" id="map.team">{thead}{team_rows(**kw)}</div>{foot}{merc_detail() if detail else ""}
	</div>"""


def merc_detail():
    attrs = [("AGI", "83"), ("DEX", "88"), ("STR", "86"), ("WIS", "83"), ("LVL", "5"),
             ("MRK", "91", True), ("MED", "12"), ("MEC", "45"), ("EXP", "22"), ("LDR", "45")]
    a = "".join(f'<div class="attr"><span class="k">{k}</span><span class="v{" up" if len(x) > 2 else ""}">{x[1]}</span></div>'
                for x in attrs for k in [x[0]])
    return """
		<div class="merc" id="map.merc">
			<div class="merc-top">
				<div class="merc-face" id="map.merc.face" title="Enter inventory (Enter)"><img src="face-7"/></div>
				<div class="merc-id">
					<span class="nick">Ivan</span><span class="full">Ivan Dolvich &#183; A.I.M.</span>
					<div class="asg"><img class="icon" src="icon-squad"/><span>Squad 1 &#183; B13 Drassen</span></div>
					<div class="stat hp"><span class="k">HP</span><span class="bar hp"><span class="fill" style="width: 82%"></span><span class="lost" style="width: 6%"></span></span><span class="v">74/90</span></div>
					<div class="stat"><span class="k">EN</span><span class="bar breath"><span class="fill" style="width: 64%"></span></span><span class="v">64</span></div>
					<div class="stat"><span class="k">MOR</span><span class="bar morale"><span class="fill" style="width: 70%"></span></span><span class="v">Good</span></div>
				</div>
			</div>
			<div class="attrs">""" + a + """</div>
			<div class="kvs">
				<div class="kv2"><span class="k">Contract</span><span class="v"><span>5.3d / 7d</span></span></div>
				<div class="kv2"><span class="k">Daily cost</span><span class="v"><span>$1,450</span></span></div>
				<div class="kv2"><span class="k">Medical deposit</span><span class="v"><span>&#8212;</span></span></div>
				<div class="kv2"><span class="k">Insured</span><span class="v"><span>Yes</span><img class="icon" src="icon-ok"/></span></div>
			</div>
			<div class="merc-acts">
				<button class="btn" id="map.merc.inventory"><img class="icon" src="icon-inventory"/><span class="label">Gear</span><span class="kbd">Enter</span></button>
				<button class="btn" id="map.merc.assign"><img class="icon" src="icon-squad"/><span class="label">Assign</span><span class="kbd">Alt+A</span></button>
				<button class="btn last" id="map.merc.contract"><img class="icon" src="icon-contract"/><span class="label">Contract</span><span class="kbd">C</span></button>
			</div>
		</div>"""


# ------------------------------------------------------------------ sector dock
def sector_dock():
    return """
	<div class="dock dock-r" id="map.sectordock">
		<div class="dock-head"><span class="t">Sector</span><span class="m">Right-click a sector</span>
			<button class="icon-btn" id="map.sector.collapse" title="Hide the sector panel"><img class="icon" src="icon-chevron-right"/></button>
		</div>
		<div class="sector" id="map.sector">
			<div class="sec-head"><span class="sec-code">B13</span><div class="sec-name"><span class="n">Drassen</span><span class="s">Town &#183; airport &#183; yours</span></div></div>
			<div class="box" id="map.sector.forces"><span class="sec-title">Forces here</span>
				<div class="forces">
					<div class="force"><span class="n">5</span><span class="l">Mercs</span></div>
					<div class="force g"><span class="n">3</span><span class="l">Green</span></div>
					<div class="force r"><span class="n">1</span><span class="l">Regular</span></div>
					<div class="force e last"><span class="n">0</span><span class="l">Enemy</span></div>
				</div>
			</div>
			<div class="box" id="map.sector.town"><span class="sec-title">Town &#183; Drassen</span>
				<div class="row"><img class="icon" src="icon-town"/><span class="k">Control</span><span class="v ok-t">3 / 3 sectors</span></div>
				<div class="row"><img class="icon" src="icon-loyalty"/><span class="k">Loyalty</span><span class="bar ok"><span class="fill" style="width: 64%"></span></span><span class="v">64%</span></div>
				<div class="row"><img class="icon" src="icon-militia"/><span class="k">Militia (town)</span><span class="v">9 &#183; training 45%</span></div>
				<div class="chips"><div class="chip"><img class="icon" src="icon-airport"/><span>Airport</span></div><div class="chip"><img class="icon" src="icon-helicopter"/><span>Skyrider's base</span></div><div class="chip"><img class="icon" src="icon-money"/><span>Mine: D13</span></div></div>
			</div>
			<div class="box" id="map.sector.mine"><span class="sec-title">Mine &#183; D13 silver</span>
				<div class="row"><img class="icon" src="icon-mine"/><span class="k">Producing</span><span class="v ok-t">$1,800/day</span></div>
				<div class="row"><span class="k">Possible at full loyalty</span><span class="v">$2,800/day</span></div>
			</div>
			<div class="box" id="map.sector.items"><span class="sec-title">Items</span>
				<div class="row"><img class="icon" src="icon-sector-inventory"/><span class="k">In this sector</span><span class="v">37</span></div>
				<button class="btn" id="map.sector.inventory"><img class="icon" src="icon-sector-inventory"/><span class="label">Sector inventory</span><span class="kbd">Ctrl+I</span></button>
			</div>
		</div>
	</div>"""


# ------------------------------------------------------------------ the map
def map_canvas(mode="overview", selected="B13", hover=None, route=None, temp=None, heli=None, airspace=False,
               show_mines=False, show_items=False, show_militia=False, show_teams=True, battle=None, extra="", frame_extra=""):
    o = []
    o.append('<img class="map-art" src="strategic-map"/>')
    sel = sec(selected) if selected else None
    hov = sec(hover) if hover else None
    path_secs = set()
    for r in (route or []), (temp or []):
        for c in r:
            path_secs.add(sec(c))
    for y in range(1, 17):
        for x in range(1, 17):
            c = ["sec"]
            if airspace:
                bad = (x, y) in ENEMY_KNOWN or x <= 7 or y >= 6
                c.append("air-bad" if bad else "air-ok")
                if (x, y) not in VISITED:
                    c.append("fog")
            else:
                if (x, y) not in VISITED:
                    c += ["fog"]
                elif (x, y) in OURS:
                    c.append("ours")
                elif (x, y) in ENEMY_KNOWN:
                    c.append("enemy")
            if (x, y) == sel:
                c.append("sel")
            if (x, y) == hov:
                c.append("hov")
            if temp and (x, y) in path_secs and (x, y) != hov:
                c.append("path")
            l, t = px(x, y)
            idattr = f' id="map.sector[{ROWS[y - 1]}{x}]"' if (x, y) in (sel, hov) else ""
            o.append(f'<div class="{" ".join(c)}"{idattr} style="left: {l}dp; top: {t}dp;"></div>')
    # town borders
    for name, secs in TOWNS.items():
        s = set(secs)
        for (x, y) in secs:
            l, t = px(x, y)
            if (x, y - 1) not in s:
                o.append(f'<div class="tb" style="left: {l}dp; top: {t}dp; width: {SW}dp; height: 2dp;"></div>')
            if (x, y + 1) not in s:
                o.append(f'<div class="tb" style="left: {l}dp; top: {t + SH - 2}dp; width: {SW}dp; height: 2dp;"></div>')
            if (x - 1, y) not in s:
                o.append(f'<div class="tb" style="left: {l}dp; top: {t}dp; width: 2dp; height: {SH}dp;"></div>')
            if (x + 1, y) not in s:
                o.append(f'<div class="tb" style="left: {l + SW - 2}dp; top: {t}dp; width: 2dp; height: {SH}dp;"></div>')
    # mines and SAM sites
    for (x, y) in MINES:
        l, t = px(x, y)
        ours = (x, y) in OURS
        o.append(f'<div class="site{" ours" if ours else ""}" style="left: {l + SW - 23}dp; top: {t + 3}dp;" title="Mine"><img class="icon" src="icon-mine"/></div>')
        if show_mines:
            txt = "$1,800/d" if ours else ("Abandoned" if (x, y) == sec("B2") else "Enemy")
            o.append(f'<div class="mine-t" style="left: {l + SW // 2 - 55}dp; top: {t + SH - 17}dp;"><span class="{"ours" if ours else ""}">{txt}</span></div>')
    for code, side in SAMS.items():
        l, t = px(*sec(code))
        o.append(f'<div class="site {side}" style="left: {l + SW - 23}dp; top: {t + 3}dp;" title="SAM site"><img class="icon" src="icon-sam-site"/></div>')
    if battle:
        l, t = px(*sec(battle))
        o.append(f'<div class="battle" style="left: {l}dp; top: {t}dp;"></div>')
    # routes
    def draw_route(r, cls, eta=None):
        pts = [centre(c) for c in r]
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            if y0 == y1:
                a, b = sorted((x0, x1))
                o.append(f'<div class="rt {cls}" style="left: {a}dp; top: {y0 - 2}dp; width: {b - a}dp; height: 4dp;"></div>')
            else:
                a, b = sorted((y0, y1))
                o.append(f'<div class="rt {cls}" style="left: {x0 - 2}dp; top: {a}dp; width: 4dp; height: {b - a}dp;"></div>')
        for (x, y) in pts[1:-1]:
            o.append(f'<div class="wp {cls}" style="left: {x - 5}dp; top: {y - 5}dp;"></div>')
        x, y = pts[-1]
        o.append(f'<div class="dest {cls}" style="left: {x - 13}dp; top: {y - 13}dp;"><img class="icon" src="icon-destination"/></div>')
        if eta:
            o.append(f'<div class="eta-tag {cls}" style="left: {x + 14}dp; top: {y - 20}dp;">{eta}</div>')
    if route:
        draw_route(route, "", "15:55")
    if temp:
        draw_route(temp, "temp", temp_eta)
    if heli:
        draw_route(heli, "heli", "$1,500 &#183; 2:10")
    # town names
    for name, secs in TOWNS.items():
        xs = [x for x, _ in secs]
        ys = [y for _, y in secs]
        cx = (min(xs) - 1) * SW + (max(xs) - min(xs) + 1) * SW // 2
        top = (min(ys) - 1) * SH - 20 if min(ys) > 1 else 3
        side = TOWN_SIDE.get(name, "")
        loy = f'<span class="l ok-t">{LOYALTY[name]}%</span>' if name in LOYALTY else ""
        o.append(f'<div class="town-lbl {side}" style="left: {cx - 90}dp; top: {top}dp;"><span class="n">{TOWN_NAMES[name]}</span>{loy}</div>')
    # markers
    for y in range(1, 17):
        for x in range(1, 17):
            code = f"{ROWS[y - 1]}{x}"
            mk = []
            if show_teams and code in TEAMS:
                s = " sel" if code == "B13" else ""
                mk.append(f'<div class="mk team{s}" title="Your mercs"><img class="icon" src="icon-player-group"/><span>{TEAMS[code]}</span></div>')
            if code in MILITIA:
                mk.append(f'<div class="mk militia" title="Militia"><img class="icon" src="icon-militia"/><span>{MILITIA[code]}</span></div>')
            if code in ENEMIES and (show_teams or show_militia):
                mk.append(f'<div class="mk enemy" title="Enemies"><img class="icon" src="icon-enemy"/><span>{ENEMIES[code]}</span></div>')
            if show_items and code in ITEMS:
                mk.append(f'<div class="mk items" title="Items"><img class="icon" src="icon-sector-inventory"/><span>{ITEMS[code]}</span></div>')
            if mk:
                l, t = px(x, y)
                o.append(f'<div class="mks" style="left: {l + 2}dp; top: {t + SH - 42}dp;">{"".join(mk)}</div>')
    # the helicopter and the arrival point
    l, t = px(*sec("B13"))
    o.append(f'<div class="site heli" style="left: {l + SW - 23}dp; top: {t + 3}dp;" title="Skyrider"><img class="icon" src="icon-helicopter"/></div>')
    if airspace:
        o.append(f'<div class="site arrive" style="left: {l + 3}dp; top: {t + 3}dp;" title="Arrivals land here"><img class="icon" src="icon-destination"/></div>')
    cols = "".join(f'<span class="{"hl" if sel and sel[0] == i else ""}" style="left: {(i - 1) * SW}dp;">{i}</span>' for i in range(1, 17))
    rows = "".join(f'<span class="{"hl" if sel and sel[1] == i else ""}" style="top: {(i - 1) * SH}dp;">{ROWS[i - 1]}</span>' for i in range(1, 17))
    return f"""
		<div class="map-frame" id="map.frame">
			<div class="map-cols">{cols}</div>
			<div class="map-rows">{rows}</div>
			<div class="map-canvas{" airspace" if airspace else ""}" id="map.canvas">{"".join(o)}{extra}</div>{frame_extra}
		</div>"""


temp_eta = "18:40"
ITEMS = {"B13": "37", "C13": "12", "A9": "58", "A10": "6", "D13": "3"}


def map_tools(legend=False):
    lg = ""
    if legend:
        lg = """<div class="legend" id="map.legend">
			<div class="li"><div class="mk team"><img class="icon" src="icon-player-group"/><span>5</span></div><span>Your mercs</span></div>
			<div class="li"><div class="mk militia"><img class="icon" src="icon-militia"/><span>4</span></div><span>Militia</span></div>
			<div class="li"><div class="mk enemy"><img class="icon" src="icon-enemy"/><span>?</span></div><span>Enemies (? = not scouted)</span></div>
			<div class="li"><div class="sw" style="background-color: #0B0F0DB0;"></div><span>Not explored</span></div>
		</div>"""
    return f"""
		<div class="levels" id="map.levels" title="Map level: Insert up, Delete down"><span class="cap">Level</span><span class="lvl on" id="map.level[0]">Surface</span><span class="lvl" id="map.level[1]">-1</span><span class="lvl" id="map.level[2]">-2</span><span class="lvl off" id="map.level[3]">-3</span></div>
		<div class="map-tools" id="map.zoom">
			<button class="icon-btn" id="map.zoom.out" title="Zoom out (wheel)"><img class="icon" src="icon-remove"/></button>
			<span class="zoom">100%</span>
			<button class="icon-btn" id="map.zoom.in" title="Zoom in (wheel)"><img class="icon" src="icon-add"/></button>
			<button class="icon-btn" id="map.zoom.fit" title="Fit the map (Home)"><img class="icon" src="icon-map"/></button>
			<button class="icon-btn" id="map.legend.toggle" title="Legend"><img class="icon" src="icon-info"/></button>
		</div>{lg}"""


def mapview(canvas, overlay="", legend=False):
    return f"""
	<div class="mapview" id="map.view">
		<div class="mapview-bg"></div>{canvas}{map_tools(legend)}{overlay}
	</div>"""


def bottom(msg="Barry has finished training militia in C13: 3 new green militia.", new=3, hint=True):
    h = ('<span class="hint">Click: select sector &#183; click again: move &#183; right-click: sector info &#183; drag: pan &#183; wheel: zoom</span>'
         if hint else "")
    return f"""
	<div class="p4-bottom" id="map.log.bar">
		<button class="icon-btn" id="map.log.expand" title="Message log (Page Up)"><img class="icon" src="icon-chevron-up"/></button>
		<span class="tm">14:31</span><span class="msg" id="map.log.last">{msg}</span>
		<span class="badge info">{new} new</span>{h}
	</div>"""


def page(title, body):
    return f"""<rml>
<head>
	<title>{title} (Phase 4 wireframe)</title>
	<link type="text/rcss" href="../../components.rcss"/>
	<link type="text/rcss" href="mapscreen.rcss"/>
</head>
<!-- M2 wireframe of the native strategic map screen (docs/ui/mapscreen.md). Literal data.
     Generated by tools/ui/phase4_mocks.py: edit the generator, not this file. -->
<body class="p4">
<div class="p4-root">{body}
</div>
</body>
</rml>
"""


def layout(top, left, centre_, right, bot, extra=""):
    return f"{top}\n\t<div class=\"p4-main\">{left}{centre_}{right}\n\t</div>{bot}{extra}"


# ------------------------------------------------------------------ the states
def overview():
    return page("Strategic map", layout(topbar(), team_dock(), mapview(map_canvas(route=["C13", "C14", "D14"]), legend=True),
                                        sector_dock(), bottom()))


def plotting():
    temp = ["C13", "C14", "D14", "D15"]
    hx, hy = px(*sec("D15"))
    banner = """
		<div class="banner" id="map.plot.banner"><img class="icon" src="icon-destination"/>
			<div class="txt"><span class="t1">Plotting a route &#183; Squad 2 (3 mercs)</span>
			<span class="t2">Click D15 again to confirm, or click other sectors to add waypoints. <span class="kbd">Right-click</span> shortens, <span class="kbd">Esc</span> cancels.</span></div>
			<button class="btn" id="map.plot.cancel"><span class="label">Cancel</span><span class="kbd">Esc</span></button>
			<button class="btn btn-primary" id="map.plot.confirm"><img class="icon" src="icon-confirm"/><span class="label">Confirm</span><span class="kbd">Enter</span></button>
		</div>"""
    # the hover card sits next to the hovered sector, in map-canvas coordinates translated to the view
    card = f"""
			<div class="hovercard" id="map.hover" style="left: {hx - 250 + 24 - 8}dp; top: {hy + 24 + SH + 8}dp;">
				<div class="h"><span class="c">D15</span><span class="n">SAM site</span></div>
				<div class="row"><img class="icon" src="icon-enemy"/><span class="k">Enemies</span><span class="v danger-t">6 (scouted)</span></div>
				<div class="row"><img class="icon" src="icon-merc-moving"/><span class="k">Travel (road, on foot)</span><span class="v">4:05</span></div>
				<div class="row"><img class="icon" src="icon-destination"/><span class="k">Arrival</span><span class="v">Day 12, 18:40</span></div>
				<div class="row"><img class="icon" src="icon-warning"/><span class="k">Hostile: battle on arrival</span><span class="v warn-t">!</span></div>
			</div>"""
    canvas = map_canvas(selected="C13", hover="D15", temp=temp, frame_extra=card)
    return page("Strategic map: plotting", layout(topbar(paused=True), team_dock(selected=("Fidel",), plotting=("Fidel", "Buns", "Steroid")),
                                                   mapview(canvas, banner), sector_dock(), bottom(msg="Click again on the destination to confirm your final route, or click on another sector to place more waypoints.", new=1)))


def assignment():
    # menu next to Ivan's assignment cell (row 0 of the table: dock head 40 + thead 30 + group 26 -> y 96..128)
    menu = """
	<div class="ctx" id="map.assign" style="left: 214dp; top: 186dp;">
		<div class="menu">
			<div class="menu-head">Ivan &#183; assignment</div>
			<div class="menu-item current" id="map.assign.squad"><img class="icon" src="icon-squad"/><span class="label">Squad</span><span class="cur">1</span><img class="chev" src="icon-chevron-right"/></div>
			<div class="menu-item" id="map.assign.doctor"><img class="icon" src="icon-doctor"/><span class="label">Doctor</span></div>
			<div class="menu-item" id="map.assign.patient"><img class="icon" src="icon-patient"/><span class="label">Patient</span></div>
			<div class="menu-item" id="map.assign.vehicle"><img class="icon" src="icon-vehicle"/><span class="label">Vehicle</span><img class="chev" src="icon-chevron-right"/></div>
			<div class="menu-item" id="map.assign.repair"><img class="icon" src="icon-repair"/><span class="label">Repair</span><img class="chev" src="icon-chevron-right"/></div>
			<div class="menu-item open" id="map.assign.train"><img class="icon" src="icon-train-self"/><span class="label">Train</span><img class="chev" src="icon-chevron-right"/></div>
			<div class="menu-sep"></div>
			<div class="menu-item" id="map.assign.sleep"><img class="icon" src="icon-asleep"/><span class="label">Sleep</span></div>
			<div class="menu-item" id="map.assign.route"><img class="icon" src="icon-destination"/><span class="label">Plot travel route</span></div>
			<div class="menu-item" id="map.assign.contract"><img class="icon" src="icon-contract"/><span class="label">Contract&#8230;</span><span class="kbd">C</span></div>
			<div class="menu-item is-disabled" id="map.assign.remove"><img class="icon" src="icon-remove"/><span class="label">Remove (dead only)</span></div>
		</div>
	</div>
	<div class="ctx" id="map.assign.trainmenu" style="left: 466dp; top: 330dp;">
		<div class="menu">
			<div class="menu-head">Train</div>
			<div class="menu-item open" id="map.assign.train.self"><img class="icon" src="icon-train-self"/><span class="label">Practice</span><img class="chev" src="icon-chevron-right"/></div>
			<div class="menu-item" id="map.assign.train.town"><img class="icon" src="icon-train-town"/><span class="label">Militia</span><span class="cur">$750</span></div>
			<div class="menu-item" id="map.assign.train.teammate"><img class="icon" src="icon-train-teammate"/><span class="label">Trainer</span><img class="chev" src="icon-chevron-right"/></div>
			<div class="menu-item" id="map.assign.train.byother"><img class="icon" src="icon-train-by-other"/><span class="label">Student</span><img class="chev" src="icon-chevron-right"/></div>
		</div>
	</div>
	<div class="ctx" id="map.assign.attrmenu" style="left: 718dp; top: 330dp;">
		<div class="menu">
			<div class="menu-head">Practice which skill</div>"""
    for k, v, extra in [("Strength", 86, ""), ("Dexterity", 88, ""), ("Agility", 83, ""), ("Health", 90, ""),
                        ("Marksmanship", 91, " is-hover"), ("Medical", 12, ""), ("Mechanical", 45, ""), ("Leadership", 45, ""),
                        ("Explosives", 22, "")]:
        menu += f'<div class="menu-item{extra}" id="map.assign.train.self.{k.lower()}"><span class="label">{k}</span><span class="val">{v}</span></div>'
    menu += """
			<div class="menu-note">Ivan's current values. A trainer only teaches up to his own value.</div>
		</div>
	</div>"""
    return page("Strategic map: assignment menu",
                layout(topbar(), team_dock(), mapview(map_canvas()), sector_dock(), bottom(), menu))


def inventory():
    doll_l = [("Head", "item-177", True), ("Vest", "item-164", True), ("Legs", "", False)]
    doll_r = [("Hand", "item-25", True, True), ("Off hand", "", False, True)]
    def slot(lbl, art, big=False, cls="", count=""):
        inner = f'<div class="item"><img class="art" src="{art}"/></div>' if art else '<img class="ghost" src="icon-face-gear"/>'
        c = f'<span class="count">{count}</span>' if count else ""
        return f'<div class="slot{" big" if big else ""}{cls}">{inner}{c}<span class="lbl">{lbl}</span></div>'
    left = "".join(slot(a, b) for a, b, _ in doll_l)
    right = "".join(slot(a, b, True) for a, b, _, _ in doll_r)
    pockets = [("item-94", "3"), ("item-94", "2"), ("item-135", "2"), ("item-201", ""), ("", ""), ("", ""),
               ("item-203", ""), ("", "")]
    pk = ""
    for i, (art, cnt) in enumerate(pockets):
        cls = " drop-ok" if i == 4 else ""
        pk += slot("Pocket", art, False, cls, cnt)
    faces = [(7, "on"), (3, ""), (2, ""), (4, ""), (29, "away")]
    tabs = "".join(f'<div class="inv-tab {c}" title="{"In the sector" if c != "away" else "Not in this sector"}"><img src="face-{f}"/></div>' for f, c in [(7, "on"), (3, ""), (2, ""), (4, ""), (0, "away")])
    merc = f"""
		<div class="inv-merc" id="map.inv.merc">
			<div class="inv-tabs" id="map.inv.mercs">{tabs}</div>
			<span class="sec-title">Ivan &#183; B13 &#183; carrying 18.4 kg</span>
			<div class="doll" style="margin-top: 8dp;"><div class="doll-col">{left}</div><div class="doll-col">{right}</div></div>
			<div class="pockets" id="map.inv.pockets">{pk}</div>
			<div class="trash" id="map.inv.discard"><img class="icon" src="icon-remove"/><span>Drop here to throw away</span></div>
			<div class="inv-stats"><div class="kv2"><span class="k">Weight</span><span class="v"><span>18.4 kg / 26</span></span></div><div class="kv2"><span class="k">Camo</span><span class="v"><span>0%</span></span></div><div class="kv2"><span class="k">Armour</span><span class="v"><span>31</span></span></div></div>
		</div>"""
    cats = [("All", "misc-item", 37, True), ("Guns", "gun", 9, False), ("Ammo", "ammo", 11, False), ("Armour", "armour", 6, False),
            ("Explosives", "grenade", 4, False), ("Medical", "medkit", 3, False), ("Other", "misc-item", 4, False)]
    ct = "".join(f'<div class="cat{" on" if on else ""}" id="map.inv.cat.{n.lower()}"><img class="icon" src="icon-{ic}"/><span>{n}</span><span class="n">{c}</span></div>' for n, ic, c, on in cats)
    items = [(25, "AK-74", "", ""), (26, "AKM", "", ""), (28, "FN FAL", "", ""), (9, "MP5K", "", ""), (17, "SKS", "", ""),
             (3, "Beretta 92F", "", ""), (8, "M1911", "", ""), (31, "Remington M870", "", ""), (37, "Combat knife", "", ""),
             (94, "5.56 mm AP", "6", " dragging"), (92, "5.45 mm AP", "4", ""), (97, "7.62 WP AP", "3", ""), (71, "9 mm clip", "5", ""),
             (80, ".45 clip", "2", ""), (107, "12 gauge", "2", ""), (161, "Flak jacket", "", ""), (164, "Kevlar vest", "", ""),
             (176, "Steel helmet", "", ""), (170, "Kevlar leggings", "", ""), (135, "Hand grenade", "3", ""), (131, "Stun grenade", "1", ""),
             (141, "Mine", "", " away"), (201, "First aid kit", "2", ""), (202, "Medical kit", "", ""), (203, "Tool kit", "", ""),
             (211, "Night goggles", "", ""), (213, "Gas mask", "", ""), (214, "Canteen", "", "")]
    grid = ""
    for i, (it, nm, cnt, cls) in enumerate(items):
        tag = '<span class="tag">Buried &#183; A10</span>' if "away" in cls else ""
        c = f'<span class="count">&#215;{cnt}</span>' if cnt else ""
        grid += (f'<div class="slot{cls}" id="map.inv.item[{i}]" title="{nm}"><div class="item"><img class="art" src="item-{it}"/></div>'
                 f'{c}{tag}<span class="nm">{nm}</span><div class="cond"><div class="fill" style="width: {60 + (i * 13) % 40}%"></div></div></div>')
    pool = f"""
		<div class="inv-pool" id="map.inv.pool">
			<div class="inv-tools">
				<span class="sec-title" style="margin-right: 16dp;">Sector B13 &#183; Drassen &#183; 37 items</span>
				<div class="grow"></div>
				<div class="field-input" id="map.inv.search"><img class="icon" src="icon-search"/><span>Search items</span></div>
				<div class="dropdown" style="width: 180dp;" id="map.inv.sort"><div class="field"><span class="text">Sort: Type</span><img class="icon sm" src="icon-chevron-down"/></div></div>
			</div>
			<div class="inv-tools">{ct}</div>
			<div class="inv-grid scroll">{grid}</div>
			<div class="inv-foot"><span>Drag items onto a merc, or onto a merc's face to put them in his gear. Right-click: details.</span><div class="grow"></div>
				<button class="btn" id="map.inv.stack"><span class="label">Stack &amp; merge</span></button>
				<button class="btn btn-primary" id="map.inv.done"><img class="icon" src="icon-confirm"/><span class="label">Done</span><span class="kbd">Esc</span></button>
			</div>
		</div>"""
    area = f'\n\t<div class="inv-area" id="map.inv">{merc}{pool}\n\t</div>'
    ghost = """
	<div class="drag-ghost audit-skip" style="left: 772dp; top: 470dp;"><img src="item-94"/></div>
	<div class="drag-tip audit-skip" style="left: 772dp; top: 546dp;">Into Ivan's pocket &#183; 6 &#215; 5.56 AP</div>"""
    return page("Strategic map: sector inventory",
                layout(topbar(filters=("towns", "teams", "items")), team_dock(detail=False), area, "", bottom(), ghost))


def modal_page(title, modal_html, canvas_kw=None, top_kw=None, team_kw=None):
    body = layout(topbar(**(top_kw or {})), team_dock(**(team_kw or {})),
                  mapview(map_canvas(**(canvas_kw or {}))), sector_dock(), bottom(),
                  '\n\t<div class="scrim"></div>' + modal_html)
    return page(title, body)


def contract():
    m = """
	<div class="modal" id="map.contract" style="left: 50%; top: 50%; width: 520dp; margin-left: -260dp; margin-top: -250dp;">
		<div class="modal-head"><img class="icon status" src="icon-contract"/><span class="title">Contract &#183; Ivan</span><button class="icon-btn" id="map.contract.close" style="width: 32dp; height: 32dp;"><img class="icon sm" src="icon-close"/></button></div>
		<div class="modal-body">
			<span class="lead">5.3 of 7 days left. Ivan is happy to stay. Extending moves the money now; the balance after is shown.</span>
			<div class="opt" id="map.contract.day"><img class="icon" src="icon-contract"/><div class="l"><span class="a">Offer one day</span><span class="b">Balance after: $46,800</span></div><span class="p">$1,450</span><span class="kbd">1</span></div>
			<div class="opt is-hover" id="map.contract.week"><img class="icon" src="icon-contract"/><div class="l"><span class="a">Offer one week</span><span class="b">Balance after: $39,450 &#183; $1,257/day</span></div><span class="p">$8,800</span><span class="kbd">2</span></div>
			<div class="opt" id="map.contract.twoweeks"><img class="icon" src="icon-contract"/><div class="l"><span class="a">Offer two weeks</span><span class="b">Balance after: $32,050 &#183; $1,157/day</span></div><span class="p">$16,200</span><span class="kbd">3</span></div>
			<div class="opt danger" id="map.contract.dismiss"><img class="icon" src="icon-exit-sector"/><div class="l"><span class="a">Dismiss</span><span class="b">Ivan leaves at once; his gear stays in B13</span></div></div>
			<div class="note"><img class="icon" src="icon-info"/><span>Life insurance premium for the extension: $210 (paid with it). Merc has life insurance.</span></div>
		</div>
		<div class="modal-foot"><button class="btn btn-ghost" id="map.contract.cancel"><span class="label">Cancel</span><span class="kbd">Esc</span></button></div>
	</div>"""
    return modal_page("Strategic map: contract", m)


def movebox():
    rows = [("squad", "Squad 1", "3 mercs &#183; all selected", "on", [("Ivan", "on", ""), ("Grizzly", "on", ""), ("Lynx", "on", "")]),
            ("on-duty", "Not in a squad", "", "part", [("Vicki", "on", "Doctor: moving stops doctoring"), ("Magic", "", "Repair")]),
            ("vehicle", "Helicopter", "Skyrider &#183; use Airspace to fly it", "", [])]
    body = ""
    for ic, n, m, chk, mercs in rows:
        body += f'<div class="mv-grp"><span class="chk {chk}"></span><img class="icon" src="icon-{ic}"/><span class="n">{n}</span><span class="m">{m}</span></div>'
        for mn, c, note in mercs:
            warn = '<img class="icon" src="icon-warning"/>' if "stops" in note else ""
            body += f'<div class="mv-row{" no" if not c else ""}"><span class="chk {c}"></span><span class="n">{mn}</span><span class="m">{note}</span>{warn}</div>'
    m = f"""
	<div class="modal" id="map.move" style="left: 50%; top: 50%; width: 480dp; margin-left: -240dp; margin-top: -270dp;">
		<div class="modal-head"><img class="icon status" src="icon-merc-moving"/><span class="title">Move from B13</span></div>
		<div class="modal-body">
			<span class="lead">Pick who travels. The selected mercs form one group; you then click the destination on the map.</span>
			<div class="mv-list" id="map.move.list">{body}</div>
			<div class="note warn"><img class="icon" src="icon-warning"/><span>Mercs who move stop their assignment: Vicki stops doctoring. Sleeping mercs can't move.</span></div>
		</div>
		<div class="modal-foot"><button class="btn btn-ghost" id="map.move.cancel"><span class="label">Cancel</span><span class="kbd">Esc</span></button>
			<button class="btn btn-primary" id="map.move.plot"><img class="icon" src="icon-destination"/><span class="label">Plot route</span><span class="kbd">Enter</span></button></div>
	</div>"""
    return modal_page("Strategic map: move mercs", m)


def militia():
    grid = [("A12", "off", "Not in Drassen", 0, 0, 0), ("B12", "", "Road", 0, 0, 0), ("B13", "sel", "Town &#183; airport", 3, 1, 0),
            ("B14", "off", "Enemy held", 0, 0, 0), ("C12", "", "Wilderness", 0, 0, 0), ("C13", "drop", "Town", 3, 0, 0),
            ("C14", "", "Wilderness", 0, 0, 0), ("D12", "", "Wilderness", 0, 0, 0), ("D13", "", "Town &#183; mine", 2, 0, 0)]
    g = ""
    for code, cls, t, gr, rg, vt in grid:
        pips = lambda n, c: "".join(f'<span class="pip {c}"></span>' for _ in range(n))
        rowsh = "" if cls == "off" else (
            f'<div class="mil-row"><span class="k">Green</span><div class="pips">{pips(gr, "g")}</div><span class="v">{gr}</span></div>'
            f'<div class="mil-row"><span class="k">Regular</span><div class="pips">{pips(rg, "r")}</div><span class="v">{rg}</span></div>'
            f'<div class="mil-row"><span class="k">Veteran</span><div class="pips">{pips(vt, "v")}</div><span class="v">{vt}</span></div>')
        g += f'<div class="mil-sec {cls}" id="map.militia.sector[{code}]"><div class="h"><span class="c">{code}</span><span class="t">{t}</span></div>{rowsh}</div>'
    m = f"""
	<div class="modal" id="map.militia" style="left: 50%; top: 50%; width: 528dp; margin-left: -264dp; margin-top: -330dp;">
		<div class="modal-head"><img class="icon status" src="icon-militia"/><span class="title">Militia &#183; Drassen</span></div>
		<div class="modal-body">
			<span class="lead">Right-click a sector to pick up militia, left-click a sector to drop them. Only your town sectors next to each other take militia.</span>
			<div class="mil-grid" id="map.militia.grid">{g}</div>
			<div class="mil-pool" id="map.militia.pool"><span class="k">Picked up (unassigned)</span>
				<div class="grab"><span class="pip g"></span><span>1</span></div><div class="grab"><span class="pip r"></span><span>0</span></div><div class="grab"><span class="pip v"></span><span>0</span></div></div>
		</div>
		<div class="modal-foot"><button class="btn" id="map.militia.auto"><span class="label">Auto</span></button><div class="grow"></div>
			<button class="btn btn-primary" id="map.militia.done"><img class="icon" src="icon-confirm"/><span class="label">Done</span><span class="kbd">Enter</span></button></div>
	</div>"""
    return modal_page("Strategic map: militia", m, canvas_kw={"show_militia": True}, top_kw={"filters": ("towns", "militia")})


def helicopter():
    heli = ["B13", "B12", "B11", "B10", "A10"]
    x, y = px(*sec("A10"))
    card = f"""
			<div class="heli-card" id="map.heli.eta" style="left: {x - 300 + 24}dp; top: {y + 24 + SH + 12}dp;">
				<div class="h"><img class="icon" src="icon-helicopter"/><span>Skyrider &#183; to A10</span></div>
				<div class="row"><span class="k">Distance</span><span class="v">4 sectors</span></div>
				<div class="row"><span class="k">Safe airspace</span><span class="v ok-t">4</span></div>
				<div class="row"><span class="k">Unsafe airspace</span><span class="v">0</span></div>
				<div class="row"><span class="k">Cost</span><span class="v">$1,500</span></div>
				<div class="row"><span class="k">Arrives</span><span class="v">Day 12, 16:45 (2:10)</span></div>
				<div class="row"><span class="k">Passengers</span><span class="v">3</span></div>
				<div class="pass"><img src="face-7"/><img src="face-3"/><img src="face-2"/></div>
			</div>"""
    banner = """
		<div class="banner heli" id="map.heli.banner"><img class="icon" src="icon-helicopter"/>
			<div class="txt"><span class="t1">Airspace &#183; plotting Skyrider</span>
			<span class="t2">Green: airspace you control. Click A10 again to confirm; the bullseye (arrivals) can be dragged to another green sector.</span></div>
			<button class="btn" id="map.heli.cancel"><span class="label">Cancel</span><span class="kbd">Esc</span></button>
			<button class="btn btn-primary" id="map.heli.confirm"><img class="icon" src="icon-confirm"/><span class="label">Fly</span><span class="kbd">Enter</span></button>
		</div>"""
    canvas = map_canvas(airspace=True, heli=heli, hover="A10", show_teams=False, frame_extra=card)
    return page("Strategic map: helicopter",
                layout(topbar(filters=("towns", "airspace"), paused=True), team_dock(selected=("Ivan",), plotting=("Ivan", "Grizzly", "Lynx")),
                       mapview(canvas, banner), sector_dock(), bottom(msg="Select Skyrider or the Arrivals Drop-off?", new=1)))


def update_box():
    mercs = [(0, "Barry", "Finished training militia"), (4, "Vicki", "No more patients"), (29, "Magic", "All items repaired")]
    b = "".join(f'<div class="upd-m"><div class="f"><img src="face-{f}"/></div><span class="n">{n}</span><span class="s">{s}</span></div>' for f, n, s in mercs)
    m = f"""
	<div class="modal" id="map.update" style="left: 50%; top: 50%; width: 460dp; margin-left: -230dp; margin-top: -200dp;">
		<div class="modal-head"><img class="icon status" src="icon-info"/><span class="title">Mercs completed assignment</span></div>
		<div class="modal-body">
			<span class="lead">Time compression stopped. Give them new orders from their faces (click) or continue.</span>
			<div class="upd" id="map.update.mercs">{b}</div>
		</div>
		<div class="modal-foot"><button class="btn" id="map.update.stop"><img class="icon" src="icon-pause"/><span class="label">Stop</span><span class="kbd">Esc</span></button>
			<button class="btn btn-primary" id="map.update.continue"><img class="icon" src="icon-play"/><span class="label">Continue</span><span class="kbd">Space</span></button></div>
	</div>"""
    return modal_page("Strategic map: update box", m, top_kw={"paused": True})


def msglog():
    days = [("Day 12", [
        ("14:35", "warn", "warning", "Enemy patrol spotted moving towards C13.", "C13", True),
        ("14:31", "ok", "militia", "Barry has finished training militia in C13: 3 new green militia.", "C13", True),
        ("14:20", "", "repair", "Magic has finished repairing the items in B13.", "B13", True),
        ("13:05", "", "money", "Daily income: $3,450 from the Drassen mine.", "", False),
        ("12:40", "", "contract", "Steroid's contract runs out in 13 hours.", "", False),
        ("11:55", "danger", "error", "Lynx was wounded in D13 (bloodcat attack).", "D13", False),
        ("11:52", "", "info", "Battle in D13 won. 2 bloodcats killed.", "D13", False),
        ("09:10", "", "helicopter", "Skyrider was paid $1,500.", "", False),
        ("08:00", "", "info", "Vicki is on duty as a doctor in B13.", "B13", False)]),
        ("Day 11", [
        ("23:15", "", "asleep", "Barry is catching some Z's.", "C13", False),
        ("18:40", "ok", "town", "Drassen is now fully under your control.", "D13", False),
        ("18:02", "danger", "error", "The enemy has taken over sector B14 uncontested.", "B14", False),
        ("16:30", "", "in-transit", "Scope has been hired and will arrive at 18:00 on Day 12.", "", False)])]
    body = ""
    for d, ms in days:
        body += f'<span class="lday">{d}</span>'
        for i, (t, c, ic, x, go, new) in enumerate(ms):
            g = f'<span class="go" title="Show on the map">{go}</span>' if go else ""
            body += f'<div class="lm {c}{" new" if new else ""}"><span class="tm">{t}</span><img class="icon" src="icon-{ic}"/><span class="x">{x}</span>{g}</div>'
    log = f"""
	<div class="log" id="map.log">
		<div class="log-head"><span class="t">Messages</span>
			<div class="tabs"><span class="tab active" id="map.log.all">All</span><span class="tab" id="map.log.combat">Combat</span><span class="tab" id="map.log.team">Team</span><span class="tab" id="map.log.money">Money</span></div>
			<div class="field-input" id="map.log.search"><img class="icon sm" src="icon-search"/><span>Search</span></div>
			<button class="icon-btn" id="map.log.collapse" title="Close (Page Down / Esc)"><img class="icon" src="icon-chevron-down"/></button>
		</div>
		<div class="log-body scroll" id="map.log.list">{body}</div>
	</div>"""
    return page("Strategic map: message log",
                layout(topbar(), team_dock(), mapview(map_canvas()), sector_dock(), bottom(), log))


STATES = {
    "overview": overview, "plotting": plotting, "assignment": assignment, "inventory": inventory,
    "contract": contract, "movebox": movebox, "militia": militia, "helicopter": helicopter,
    "update": update_box, "msglog": msglog,
}

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for name, fn in STATES.items():
        with open(os.path.join(OUT, name + ".rml"), "w", encoding="utf8", newline="\n") as f:
            f.write(fn())
        print("wrote", name)
