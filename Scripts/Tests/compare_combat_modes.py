"""Compare hit-chance rolls (GDD §5.3, §9.1) with projectile combat (§16).

Inputs:
  * firing-range demo output (squad.DemoCombat, Scripts/CombatModeDemo.ps1): Saved/Demo/Combat/DemoCombat_*_{trials,engagements,shots}.csv
  * AI test matches (Scripts/AIMatchBatch.ps1 with -Rolls and without): two folders of *.json + *_shots.csv

Run with the engine's bundled Python:
  "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" -I Scripts/Tests/compare_combat_modes.py \
      --demo Saved/Demo/Combat --rolls Saved/MatchStats/cmp_rolls_4v4 --projectile Saved/MatchStats/cmp_projectile_4v4 \
      --out Docs/Balancing/<date>_combat_modes/README.md --json Docs/Balancing/<date>_combat_modes/data/summary.json
"""
import argparse
import csv
import glob
import json
import os
import statistics
from collections import Counter, defaultdict

MODES = ("rolls", "projectile")
LABEL = {"rolls": "Rolls", "projectile": "Projectile"}


def pct(a, b, digits=0):
    return round(100.0 * a / b, digits) if b else None


def fmt(v, suffix="%"):
    return "-" if v is None else f"{v:g}{suffix}"


def median(xs):
    return statistics.median(xs) if xs else None


def mean(xs):
    return statistics.mean(xs) if xs else None


def r1(v):
    return None if v is None else round(v, 1)


def read_csv(path):
    with open(path, encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


# ------------------------------------------------------------------------------------------------ firing range demo

TARGET_ORDER = ["open_standing", "open_crouched", "open_prone", "hay_braced", "hay_prone", "lowwall_braced", "lowwall_prone",
                "window_braced", "window_standing", "tree_crouched"]
TARGET_LABEL = {
    "open_standing": "Open, standing", "open_crouched": "Open, crouched", "open_prone": "Open, prone",
    "hay_braced": "Hay bale, braced", "hay_prone": "Hay bale, prone", "lowwall_braced": "Low wall, braced",
    "lowwall_prone": "Low wall, prone", "window_braced": "Window, braced", "window_standing": "Window, standing",
    "tree_crouched": "Tree, crouched",
}
IN_COVER_BRACED = ("hay_braced", "lowwall_braced", "window_braced")
OPEN = ("open_standing", "open_crouched", "open_prone")


def load_demo(folder):
    trials, engages, shots = [], [], []
    for f in sorted(glob.glob(os.path.join(folder, "DemoCombat_*_trials.csv"))):
        trials += [r for r in read_csv(f) if r["test"] == "hitrate"]
    for f in sorted(glob.glob(os.path.join(folder, "DemoCombat_*_engagements.csv"))):
        engages += read_csv(f)
    for f in sorted(glob.glob(os.path.join(folder, "DemoCombat_*_shots.csv"))):
        shots += [r for r in read_csv(f) if r["test"] == "hitrate"]
    return trials, engages, shots


def demo_section(trials, engages, shots, p, data):
    p("## 1. Firing range (controlled)\n")
    p("Same lane, same soldiers, both rule sets back to back: Allied soldier 1 (M1 Garand) fires at Axis soldier 1, who "
      "holds fire. Stress held at 0 on both. Lanes are straight on: the shooter is square in front of the cover. "
      f"{len(trials)} hit-rate trials ({sum(int(t['rounds']) for t in trials)} rounds) and {len(engages)} engagements.\n")

    # hit rate[target][shooter_stance][mode][range] = (hits, rounds, shown)
    # Trials of the same set-up from several runs (parallel processes) are pooled.
    acc = defaultdict(lambda: {"hits": 0, "rounds": 0, "los": False, "chance": 0.0, "shown0": 0.0, "exp": 0.0, "stops": 0,
                               "parts": Counter(), "runs": 0})
    ranges = sorted({int(t["range_m"]) for t in trials})
    for t in trials:
        n = int(t["rounds"])
        a = acc[(t["target"], t["shooter_stance"], t["mode"], int(t["range_m"]))]
        a["hits"] += int(t["hits"]); a["rounds"] += n; a["los"] |= t["los"] == "1"; a["runs"] += 1
        a["chance"] += float(t["mean_chance"]) * n; a["shown0"] += float(t["shown_chance"])
        a["exp"] += max(float(t["mean_exposure"]), 0.0) * n; a["stops"] += int(t["cover_stops"])
        a["parts"].update({k: int(t[k]) for k in ("head", "chest", "belly", "hips", "thighs", "shins")})
    hr = defaultdict(lambda: defaultdict(lambda: defaultdict(dict)))
    for (tgt, sh, m, r), a in acc.items():
        n = a["rounds"]
        hr[tgt][sh][m][r] = {
            "hits": a["hits"], "rounds": n, "los": a["los"], "hit_pct": pct(a["hits"], n, 1),
            "shown": r1(a["chance"] / n) if n else r1(a["shown0"] / a["runs"]),
            "exposure": r1(a["exp"] / n) if n and m == "projectile" else None,
            "cover_stops": pct(a["stops"], n, 1), "parts": dict(a["parts"]),
        }
    data["range_hit_rates"] = hr
    rounds_per = int(median([c["rounds"] for t in hr.values() for sh in t.values() for m in sh.values() for c in m.values() if c["rounds"]]))
    data["ranges"] = ranges

    for shooter in ("standing", "prone"):
        p(f"### Hit rate by range, shooter {shooter}\n")
        p(f"Each cell: **rolls / projectile** hit rate ({rounds_per} rounds each). `nl` = no line of sight (can't fire), `-` = no lane on the map.\n")
        p("| Target | " + " | ".join(f"{r} m" for r in ranges) + " |")
        p("|---|" + "---|" * len(ranges))
        for tgt in TARGET_ORDER:
            if tgt not in hr:
                continue
            cells = []
            for r in ranges:
                parts = []
                for m in MODES:
                    c = hr[tgt][shooter][m].get(r)
                    parts.append("-" if c is None else ("nl" if not c["los"] and c["rounds"] == 0 else f"{c['hit_pct']:.0f}"))
                cells.append(" / ".join(parts))
            p(f"| {TARGET_LABEL.get(tgt, tgt)} | " + " | ".join(cells) + " |")
        p("")

    # Open vs braced-in-cover, pooled.
    p("### Out of cover vs in cover (pooled, shooter standing)\n")
    p("Out of cover = open standing / crouched / prone pooled; in cover = braced behind hay, low wall or window pooled. "
      "Only ranges where every pooled set-up has a lane.\n")
    p("| Range | Out of cover: rolls | Out: projectile | In cover: rolls | In: projectile | Cover protection rolls | Cover protection projectile |")
    p("|---|---|---|---|---|---|---|")
    pooled = {}
    for r in ranges:
        row = {}
        for grp_name, grp in (("open", OPEN), ("cover", IN_COVER_BRACED)):
            for m in MODES:
                cs = [hr[t]["standing"][m].get(r) for t in grp]
                if any(c is None for c in cs):
                    row[(grp_name, m)] = None
                    continue
                h = sum(c["hits"] for c in cs)
                n = sum(max(c["rounds"], 1 if not c["los"] else 0) for c in cs)
                row[(grp_name, m)] = pct(h, n, 1)
        def prot(m):
            o, c = row.get(("open", m)), row.get(("cover", m))
            return None if o in (None, 0) or c is None else round(100 * (1 - c / o))
        pooled[r] = {f"{g}_{m}": row.get((g, m)) for g in ("open", "cover") for m in MODES}
        pooled[r]["protection_rolls"] = prot("rolls")
        pooled[r]["protection_projectile"] = prot("projectile")
        p(f"| {r} m | {fmt(row[('open', 'rolls')])} | {fmt(row[('open', 'projectile')])} | {fmt(row[('cover', 'rolls')])} | "
          f"{fmt(row[('cover', 'projectile')])} | {fmt(prot('rolls'))} | {fmt(prot('projectile'))} |")
    data["pooled_cover"] = pooled
    p("\n_Cover protection = how much of the open-ground hit rate the cover takes away._\n")

    # Calibration of shown chance (projectile estimate vs actual).
    p("### Does the shown hit chance match? (shooter standing)\n")
    p("| Target | Mode | Mean shown | Actual | Gap (actual - shown) |")
    p("|---|---|---|---|---|")
    calib = {}
    for tgt in TARGET_ORDER:
        if tgt not in hr:
            continue
        for m in MODES:
            cs = [c for c in hr[tgt]["standing"][m].values() if c["rounds"]]
            if not cs:
                continue
            n = sum(c["rounds"] for c in cs)
            shown = sum(c["shown"] * c["rounds"] for c in cs) / n
            act = 100.0 * sum(c["hits"] for c in cs) / n
            calib[f"{tgt}/{m}"] = (round(shown, 1), round(act, 1))
            p(f"| {TARGET_LABEL[tgt]} | {LABEL[m]} | {shown:.1f}% | {act:.1f}% | {act - shown:+.1f} |")
    data["calibration"] = calib

    # Where projectile hits land, by target.
    p("\n### Projectile model: where the rounds go (all ranges, shooter standing)\n")
    p("| Target | Hit | Stopped by cover | Missed past | Hits on head | chest | belly | hips/legs |")
    p("|---|---|---|---|---|---|---|---|")
    parts_out = {}
    for tgt in TARGET_ORDER:
        cs = [c for c in hr.get(tgt, {}).get("standing", {}).get("projectile", {}).values() if c["rounds"]]
        if not cs:
            continue
        n = sum(c["rounds"] for c in cs)
        h = sum(c["hits"] for c in cs)
        stops = sum(round(c["cover_stops"] * c["rounds"] / 100) for c in cs)
        parts = Counter()
        for c in cs:
            parts.update(c["parts"])
        hp = sum(parts.values()) or 1
        legs = parts["hips"] + parts["thighs"] + parts["shins"]
        parts_out[tgt] = {k: pct(v, hp) for k, v in parts.items()}
        p(f"| {TARGET_LABEL[tgt]} | {pct(h, n)}% | {pct(stops, n)}% | {pct(n - h - stops, n)}% | {pct(parts['head'], hp)}% | "
          f"{pct(parts['chest'], hp)}% | {pct(parts['belly'], hp)}% | {pct(legs, hp)}% |")
    data["hit_parts"] = parts_out

    # Engagements.
    p("\n### Engagements: injure, then finish (shooter standing)\n")
    p(f"The target is shot for real (§5.1): the first hit injures it and it drops prone where it is; the next hit kills. "
      f"A hit from 5 m or closer kills outright. Up to 45 s per engagement; \"finished\" = killed in that time. "
      f"\"Injured, then hidden\" = once prone it couldn't be seen any more.\n")
    p("| Target | Range | Mode | Finished | Injured, then hidden | Injured, time ran out | Never hit | Median rounds to injure | Median s to injure | Median rounds to kill | Median s to kill |")
    p("|---|---|---|---|---|---|---|---|---|---|---|")
    eng = defaultdict(list)
    for e in engages:
        eng[(e["target"], int(e["range_m"]), e["mode"])].append(e)
    eng_out = {}
    for tgt in TARGET_ORDER:
        for r in sorted({k[1] for k in eng if k[0] == tgt}):
            for m in MODES:
                es = eng.get((tgt, r, m))
                if not es:
                    continue
                n = len(es)
                res = Counter(e["result"] for e in es)
                fin = res["killed"] + res["killed_outright"]
                inj = [int(e["rounds_to_injure"]) for e in es if int(e["rounds_to_injure"]) > 0]
                tinj = [float(e["time_to_injure"]) for e in es if float(e["time_to_injure"]) >= 0]
                kil = [int(e["rounds_to_kill"]) for e in es if int(e["rounds_to_kill"]) > 0]
                tkil = [float(e["time_to_kill"]) for e in es if float(e["time_to_kill"]) >= 0]
                eng_out[f"{tgt}/{r}/{m}"] = {
                    "n": n, "finished": pct(fin, n), "hidden": pct(res["injured_hidden"], n), "timeout": pct(res["injured_timeout"], n),
                    "never_hit": pct(res["untouched"] + res["no_los"], n), "rounds_to_injure": median(inj), "time_to_injure": r1(median(tinj)),
                    "rounds_to_kill": median(kil), "time_to_kill": r1(median(tkil)),
                }
                d = eng_out[f"{tgt}/{r}/{m}"]
                p(f"| {TARGET_LABEL[tgt]} | {r} m | {LABEL[m]} | {d['finished']}% | {d['hidden']}% | {d['timeout']}% | {d['never_hit']}% | "
                  f"{fmt(d['rounds_to_injure'], '')} | {fmt(d['time_to_injure'], ' s')} | {fmt(d['rounds_to_kill'], '')} | {fmt(d['time_to_kill'], ' s')} |")
    data["engagements"] = eng_out
    p("")


# ------------------------------------------------------------------------------------------------ AI matches

BANDS = [(5, "0-5 m"), (8, "5-8 m"), (12, "8-12 m"), (20, "12-20 m"), (30, "20-30 m"), (99, "30-40 m")]


def band(d):
    for hi, name in BANDS:
        if d <= hi:
            return name
    return BANDS[-1][1]


def load_matches(folder):
    matches = []
    for f in sorted(glob.glob(os.path.join(folder, "*.json"))):
        with open(f, encoding="utf-8") as fh:
            m = json.load(fh)
        c = f[:-5] + "_shots.csv"
        m["_shots"] = read_csv(c) if os.path.exists(c) else []
        matches.append(m)
    return matches


def is_hit(r):
    return r["hit"] == "1"


def match_stats(matches, mode):
    s = {}
    n = len(matches)
    s["matches"] = n
    wins = Counter(m["winner"] for m in matches)
    reasons = Counter(m["reason"] for m in matches)
    s["allies_wins"], s["axis_wins"], s["no_result"] = wins["Allies"], wins["Axis"], wins["none"]
    s["timeouts"] = reasons["timeout"]
    s["by_reason"] = dict(reasons)
    durs = [m["duration_s"] for m in matches]
    s["median_length_s"] = r1(median(durs))
    s["decided_median_length_s"] = r1(median([m["duration_s"] for m in matches if m["reason"] != "timeout"]))
    shots = [r for m in matches for r in m["_shots"]]
    s["shots_per_match"] = r1(len(shots) / n)
    hits = [r for r in shots if is_hit(r)]
    s["hit_rate"] = pct(len(hits), len(shots), 1)
    soldiers = [x for m in matches for x in m["soldiers"]]
    deaths = [x for x in soldiers if x["end_state"] == "dead"]
    s["deaths_per_match"] = r1(len(deaths) / n)
    s["death_share"] = pct(len(deaths), len(soldiers), 1)
    cause = Counter(x["death_cause"] for x in deaths)
    s["death_causes"] = dict(cause)
    s["end_injured_per_match"] = r1(sum(1 for x in soldiers if x["end_state"] == "injured") / n)
    rifle_kills = cause.get("rifle", 0)
    s["shots_per_rifle_kill"] = r1(len(shots) / rifle_kills) if rifle_kills else None
    s["rounds_fired_per_soldier"] = r1(mean([x["rounds_fired"] for x in soldiers]))
    s["mean_shown_chance"] = r1(mean([float(r["final_chance"]) for r in shots]))
    # Stress
    tiers = [0.0] * 4
    for x in soldiers:
        for i, v in enumerate(x["stress_tier_seconds"]):
            tiers[i] += v
    tot = sum(tiers) or 1
    s["stress_tier_share"] = [round(100 * t / tot, 1) for t in tiers]
    s["mean_peak_stress"] = r1(mean([x["peak_stress"] for x in soldiers]))
    # Grenades
    teams = [t for m in matches for t in m["teams"]]
    s["grenades_per_match"] = r1(sum(t["grenades_thrown"] for t in teams) / n)
    s["grenade_kills_per_match"] = r1(sum(t["blast_kills"] for t in teams) / n)
    s["grenade_stuns_per_match"] = r1(sum(t["blast_stuns"] for t in teams) / n)
    if mode == "projectile":
        s["friendly_fire_hits_per_match"] = r1(sum(t.get("friendly_fire_hits", 0) for t in teams) / n)
        s["stray_enemy_hits_per_match"] = r1(sum(t.get("stray_enemy_hits", 0) for t in teams) / n)
        s["rounds_stopped_by_cover_share"] = pct(sum(t.get("rounds_stopped_by_cover", 0) for t in teams), len(shots), 1)
        parts = Counter()
        for t in teams:
            parts.update(t.get("target_hit_parts", {}))
        tp = sum(parts.values()) or 1
        s["hit_parts"] = {k: pct(v, tp) for k, v in parts.items()}
    s["medkits_per_match"] = r1(sum(x["medkits_given"] for x in soldiers) / n)

    # Hit rate by cover x range
    cov = defaultdict(lambda: [0, 0])
    covband = defaultdict(lambda: [0, 0])
    for r in shots:
        c = "in_cover" if r["cover"] not in ("None", "") else "open"
        b = band(float(r["distance_m"]))
        cov[c][0] += is_hit(r); cov[c][1] += 1
        covband[(c, b)][0] += is_hit(r); covband[(c, b)][1] += 1
    s["hit_rate_by_cover"] = {k: {"hit_pct": pct(v[0], v[1], 1), "shots": v[1]} for k, v in cov.items()}
    s["hit_rate_by_cover_band"] = {f"{c}|{b}": {"hit_pct": pct(v[0], v[1], 1), "shots": v[1]} for (c, b), v in covband.items()}
    covtype = defaultdict(lambda: [0, 0])
    for r in shots:
        covtype[r["cover"] or "None"][0] += is_hit(r); covtype[r["cover"] or "None"][1] += 1
    s["hit_rate_by_cover_type"] = {k: {"hit_pct": pct(v[0], v[1], 1), "shots": v[1]} for k, v in covtype.items()}
    stance = defaultdict(lambda: [0, 0])
    for r in shots:
        k = r["target_stance"] + (" moving" if r["target_moving"] == "1" else "")
        stance[k][0] += is_hit(r); stance[k][1] += 1
    s["hit_rate_by_target_stance"] = {k: {"hit_pct": pct(v[0], v[1], 1), "shots": v[1]} for k, v in stance.items()}

    # What hits do, in and out of cover: injured vs killed (outright <= 5 m, or finishing an injured soldier)
    out = defaultdict(Counter)
    for r in hits:
        c = "in_cover" if r["cover"] not in ("None", "") else "open"
        o = r["outcome"]
        if o == "killed":
            was_inj = r.get("target_injured")
            if was_inj in ("0", "1"):
                o = "killed_finish" if was_inj == "1" else "killed_outright"
            else:
                o = "killed_outright" if float(r["distance_m"]) <= 5 else "killed_finish"
        out[c][o] += 1
        out["all"][o] += 1
    s["hit_outcomes"] = {c: dict(v) for c, v in out.items()}

    # Shot calibration by band (shown vs actual)
    cal = defaultdict(lambda: [0.0, 0, 0])
    for r in shots:
        b = band(float(r["distance_m"]))
        cal[b][0] += float(r["final_chance"]); cal[b][1] += 1; cal[b][2] += is_hit(r)
    s["calibration_by_band"] = {b: {"shown": r1(v[0] / v[1]), "actual": pct(v[2], v[1], 1), "shots": v[1]} for b, v in cal.items()}

    # Fate of every injury: finished off, treated, still lying injured at the end, or the match ended / other
    fates = Counter()
    inj_to_death = []
    for m in matches:
        injuries = defaultdict(list)
        for r in m["_shots"]:
            if is_hit(r) and r["outcome"] == "injured":
                injuries[r["target"]].append(float(r["time"]))
        for e in m["events"]:
            parts = e.split()
            if len(parts) > 3 and parts[1] in ("friendly_fire", "stray_round") and e.endswith("injured"):
                injuries[parts[4]].append(float(parts[0]))
        treated = defaultdict(list)
        for e in m["events"]:
            parts = e.split()
            if len(parts) >= 5 and parts[1] == "medkit":
                treated[parts[4]].append(float(parts[0]))
        by_id = {x["id"]: x for x in m["soldiers"]}
        for sid, times in injuries.items():
            x = by_id.get(sid)
            for i, t in enumerate(sorted(times)):
                nxt = sorted(times)[i + 1] if i + 1 < len(times) else 1e9
                heal = [h for h in treated.get(sid, []) if t < h < nxt]
                if heal:
                    fates["treated"] += 1
                elif x and x["end_state"] == "dead" and t <= x["death_time"] < nxt + 0.01:
                    fates["killed_" + (x["death_cause"] or "rifle")] += 1
                    inj_to_death.append(x["death_time"] - t)
                elif x and x["end_state"] == "injured":
                    fates["injured_at_end"] += 1
                else:
                    fates["other"] += 1
    s["injury_fates"] = dict(fates)
    s["injuries_per_match"] = r1(sum(fates.values()) / n)
    s["median_injury_to_death_s"] = r1(median(inj_to_death))
    return s


def ai_section(rolls, proj, p, data):
    p("## 2. AI test matches (4v4 Farmstead, bots only)\n")
    sr = match_stats(rolls, "rolls")
    sp = match_stats(proj, "projectile")
    data["ai"] = {"rolls": sr, "projectile": sp}
    p(f"{sr['matches']} matches per rule set on the same seeds. Bots decide these fights, so this shows how each model plays under bot tactics.\n")

    def row(name, k, suffix=""):
        p(f"| {name} | {fmt(sr.get(k), suffix)} | {fmt(sp.get(k), suffix)} |")
    p("| | Rolls | Projectile |")
    p("|---|---|---|")
    p(f"| Result (Allies / Axis / none) | {sr['allies_wins']} / {sr['axis_wins']} / {sr['no_result']} | {sp['allies_wins']} / {sp['axis_wins']} / {sp['no_result']} |")
    row("Timeouts (stalemates)", "timeouts", "")
    row("Median match length", "median_length_s", " s")
    row("Median length, decided matches", "decided_median_length_s", " s")
    row("Shots per match", "shots_per_match", "")
    row("Hit rate, all shots", "hit_rate")
    row("Mean shown hit chance", "mean_shown_chance")
    row("Shots per rifle kill", "shots_per_rifle_kill", "")
    row("Deaths per match (of 32)", "deaths_per_match", "")
    row("Injuries per match", "injuries_per_match", "")
    row("Still lying injured at the end, per match", "end_injured_per_match", "")
    row("Median injury -> death", "median_injury_to_death_s", " s")
    row("Medkits used per match", "medkits_per_match", "")
    row("Grenades thrown per match", "grenades_per_match", "")
    row("Grenade kills per match", "grenade_kills_per_match", "")
    row("Rounds fired per soldier", "rounds_fired_per_soldier", "")
    row("Mean peak stress", "mean_peak_stress", "")
    p(f"| Death causes | {sr['death_causes']} | {sp['death_causes']} |")
    if "friendly_fire_hits_per_match" in sp:
        p(f"| Friendly-fire hits per match | n/a | {sp['friendly_fire_hits_per_match']} |")
        p(f"| Rounds stopped by cover | n/a | {sp['rounds_stopped_by_cover_share']}% |")
    p("")

    p("### Hit rate in and out of cover, by range\n")
    p("Cover = the grid cover the target had against the shooter (any type). Cells: hit rate (shots).\n")
    p("| Range | Open: rolls | Open: projectile | In cover: rolls | In cover: projectile |")
    p("|---|---|---|---|---|")
    for _, b in BANDS:
        cells = []
        for c in ("open", "in_cover"):
            for s in (sr, sp):
                v = s["hit_rate_by_cover_band"].get(f"{c}|{b}")
                cells.append("-" if not v else f"{v['hit_pct']}% ({v['shots']})")
        p(f"| {b} | " + " | ".join(cells) + " |")
    p("")
    p("### By target stance\n")
    p("| Target | Rolls | Projectile |")
    p("|---|---|---|")
    for k in sorted(set(sr["hit_rate_by_target_stance"]) | set(sp["hit_rate_by_target_stance"])):
        cells = []
        for v in (sr["hit_rate_by_target_stance"].get(k), sp["hit_rate_by_target_stance"].get(k)):
            cells.append("-" if not v else "%s%% (%d)" % (v["hit_pct"], v["shots"]))
        p(f"| {k} | {cells[0]} | {cells[1]} |")
    p("")

    p("### What a hit does: injured vs killed, in and out of cover\n")
    p("Killed outright = hit from 5 m or closer; finished = second hit on an already injured soldier.\n")
    p("| Where | Mode | Hits | Injured | Killed outright | Finished off |")
    p("|---|---|---|---|---|---|")
    for c, name in (("open", "Out of cover"), ("in_cover", "In cover"), ("all", "All")):
        for m, s in (("rolls", sr), ("projectile", sp)):
            o = s["hit_outcomes"].get(c, {})
            tot = sum(o.values())
            p(f"| {name} | {LABEL[m]} | {tot} | {pct(o.get('injured', 0), tot)}% | {pct(o.get('killed_outright', 0), tot)}% | {pct(o.get('killed_finish', 0), tot)}% |")
    p("")

    p("### What happened to injured soldiers\n")
    p("| Fate | Rolls | Projectile |")
    p("|---|---|---|")
    keys = sorted(set(sr["injury_fates"]) | set(sp["injury_fates"]))
    tr, tp = sum(sr["injury_fates"].values()), sum(sp["injury_fates"].values())
    for k in keys:
        a, b = sr["injury_fates"].get(k, 0), sp["injury_fates"].get(k, 0)
        p(f"| {k.replace('_', ' ')} | {a} ({pct(a, tr)}%) | {b} ({pct(b, tp)}%) |")
    p("")

    p("### Shown chance vs actual, by range\n")
    p("| Range | Rolls shown | Rolls actual | Projectile shown | Projectile actual |")
    p("|---|---|---|---|---|")
    for _, b in BANDS:
        a, c = sr["calibration_by_band"].get(b), sp["calibration_by_band"].get(b)
        p(f"| {b} | {fmt(a and a['shown'])} | {fmt(a and a['actual'])} | {fmt(c and c['shown'])} | {fmt(c and c['actual'])} |")
    p("")
    p("### Stress (share of time alive per tier: calm / minor / major / shocked)\n")
    p(f"- Rolls: {sr['stress_tier_share']}  \n- Projectile: {sp['stress_tier_share']}\n")
    if "hit_parts" in sp:
        p(f"Projectile hits by body part: {sp['hit_parts']}\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--demo", default="Saved/Demo/Combat")
    ap.add_argument("--rolls", default="Saved/MatchStats/cmp_rolls_4v4")
    ap.add_argument("--projectile", default="Saved/MatchStats/cmp_projectile_4v4")
    ap.add_argument("--out", default="")
    ap.add_argument("--json", default="")
    a = ap.parse_args()
    out, data = [], {}
    p = out.append
    p("# Combat models compared: hit-chance rolls vs projectiles\n")
    trials, engages, shots = load_demo(a.demo)
    if trials or engages:
        demo_section(trials, engages, shots, p, data)
    rolls, proj = load_matches(a.rolls), load_matches(a.projectile)
    if rolls and proj:
        ai_section(rolls, proj, p, data)
    text = "\n".join(out) + "\n"
    if a.out:
        os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
        with open(a.out, "w", encoding="utf-8") as fh:
            fh.write(text)
    if a.json:
        os.makedirs(os.path.dirname(a.json) or ".", exist_ok=True)
        with open(a.json, "w", encoding="utf-8") as fh:
            json.dump(data, fh, indent=1, default=lambda o: dict(o))
    print(text)


if __name__ == "__main__":
    main()
