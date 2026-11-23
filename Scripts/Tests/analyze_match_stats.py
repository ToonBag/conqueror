"""Summarise AI test-match records (Saved/MatchStats/*.json + *_shots.csv) into balancing tables.

Run with any Python 3 (UE ships one):
  "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" -I \
      Scripts/Tests/analyze_match_stats.py [Saved/MatchStats] [--since 20261123-1550] [--out report.md]

Writes Markdown to stdout (or --out). Records come from Scripts/AIMatchBatch.ps1 (squad.MatchStats 1).
"""
import argparse
import csv
import glob
import json
import os
import statistics
from collections import Counter, defaultdict


def pct(a, b):
    return f"{100.0 * a / b:.1f}%" if b else "-"


def mean(xs):
    return statistics.mean(xs) if xs else 0.0


def band(d):
    for hi, name in ((5, "0-5 m"), (15, "5-15 m"), (30, "15-30 m"), (50, "30-50 m")):
        if d <= hi:
            return name
    return "50+ m"


BANDS = ["0-5 m", "5-15 m", "15-30 m", "30-50 m", "50+ m"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="*", default=[os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "MatchStats")],
                    help="one or more folders of records (pooled)")
    ap.add_argument("--since", default="", help="only records whose time stamp (file name prefix) is >= this")
    ap.add_argument("--out", default="")
    a = ap.parse_args()
    a.dir = ", ".join(a.dirs)

    files = sorted(f for d in a.dirs for f in glob.glob(os.path.join(d, "*.json")) if os.path.basename(f) >= a.since)
    matches, shots = [], []
    for f in files:
        with open(f, encoding="utf-8") as fh:
            m = json.load(fh)
        m["_file"] = os.path.basename(f)
        matches.append(m)
        csv_path = f[:-5] + "_shots.csv"
        if os.path.exists(csv_path):
            with open(csv_path, encoding="utf-8") as fh:
                for r in csv.DictReader(fh):
                    r["seed"] = m["seed"]
                    shots.append(r)

    out = []
    p = out.append
    if not matches:
        print("No records found in", a.dir)
        return

    n = len(matches)
    squads = Counter(m["squads_per_team"] for m in matches)
    p(f"# AI test matches: {n} on {matches[0]['map']} ({', '.join(f'{k} squads/team x{v}' for k, v in squads.items())})\n")

    # ---- Results
    p("## Results\n")
    p("| Seed | Winner | How | Length | Allies alive | Axis alive | Allies injured | Axis injured | VPs at end |")
    p("|---|---|---|---|---|---|---|---|---|")
    for m in matches:
        alive = Counter(); inj = Counter()
        for s in m["soldiers"]:
            if s["end_state"] != "dead":
                alive[s["team"]] += 1
            if s["end_state"] == "injured":
                inj[s["team"]] += 1
        vps = " ".join(f"{k}:{v[:2]}" for k, v in sorted(m["vp_owners"].items()))
        p(f"| {m['seed']} | {m['winner']} | {m['reason']} | {m['duration_s'] / 60:.1f} min | {alive['Allies']} | {alive['Axis']} "
          f"| {inj['Allies']} | {inj['Axis']} | {vps} |")
    wins = Counter(m["winner"] for m in matches)
    reasons = Counter(m["reason"] for m in matches)
    p(f"\nAllies {wins['Allies']} - Axis {wins['Axis']} - no result {wins['none']}.  "
      f"By: {', '.join(f'{k} {v}' for k, v in reasons.items())}.  "
      f"Mean length {mean([m['duration_s'] for m in matches]) / 60:.1f} min.\n")

    # ---- Casualties
    p("## Casualties\n")
    deaths = [s for m in matches for s in m["soldiers"] if s["end_state"] == "dead"]
    total_soldiers = sum(len(m["soldiers"]) for m in matches)
    cause = Counter(s["death_cause"] for s in deaths)
    p(f"- {len(deaths)} of {total_soldiers} soldiers died ({pct(len(deaths), total_soldiers)}). Causes: "
      + ", ".join(f"{k} {v} ({pct(v, len(deaths))})" for k, v in cause.most_common()) + ".")
    times = sorted(s["death_time"] for s in deaths)
    if times:
        q = lambda f: times[min(len(times) - 1, int(f * len(times)))]
        p(f"- Death times: first {times[0]:.0f} s, 25% by {q(0.25):.0f} s, half by {q(0.5):.0f} s, 75% by {q(0.75):.0f} s, last {times[-1]:.0f} s.")
    first_minute = sum(1 for t in times if t <= 60)
    p(f"- {pct(first_minute, len(times))} of all deaths happen in the first 60 s.")
    by_team = Counter(s["team"] for s in deaths)
    p(f"- Deaths by side: Allies {by_team['Allies']}, Axis {by_team['Axis']}.\n")

    # ---- Rifle fire
    p("## Rifle fire\n")
    rifle = [r for r in shots]
    hits = [r for r in rifle if r["hit"] == "1"]
    outcomes = Counter(r["outcome"] for r in hits)
    rifle_kills = cause.get("rifle", 0)
    p(f"- {len(rifle)} shots, {len(hits)} hits ({pct(len(hits), len(rifle))}). Hits: "
      + ", ".join(f"{k or 'none'} {v}" for k, v in outcomes.most_common()) + ".")
    p(f"- Shots per rifle kill: {len(rifle) / rifle_kills:.1f}." if rifle_kills else "- No rifle kills.")
    p(f"- Mean hit chance of shots taken: {mean([float(r['final_chance']) for r in rifle]):.1f}%; "
      f"actual hit rate {pct(len(hits), len(rifle))} (RNG check).")
    rounds_fired = [s["rounds_fired"] for m in matches for s in m["soldiers"]]
    rounds_left = [s.get("rounds_left", 0) for m in matches for s in m["soldiers"] if s["end_state"] != "dead"]
    p(f"- Rounds fired per soldier: mean {mean(rounds_fired):.1f}, max {max(rounds_fired)} (of 70). "
      f"Survivors end with {mean(rounds_left):.0f} rounds on average.\n")

    p("### By range\n")
    p("| Range | Shots | Mean chance | Hit rate | Kills+injuries |")
    p("|---|---|---|---|---|")
    by_band = defaultdict(list)
    for r in rifle:
        by_band[band(float(r["distance_m"]))].append(r)
    for b in BANDS:
        rs = by_band.get(b, [])
        if rs:
            h = [r for r in rs if r["hit"] == "1"]
            p(f"| {b} | {len(rs)} ({pct(len(rs), len(rifle))}) | {mean([float(r['final_chance']) for r in rs]):.1f}% | "
              f"{pct(len(h), len(rs))} | {sum(1 for r in h if r['outcome'] in ('killed', 'injured'))} |")

    p("\n### By target cover\n")
    p("| Cover | Shots | Mean chance | Hit rate |")
    p("|---|---|---|---|")
    by_cover = defaultdict(list)
    for r in rifle:
        by_cover[r["cover"]].append(r)
    for c, rs in sorted(by_cover.items(), key=lambda kv: -len(kv[1])):
        h = sum(1 for r in rs if r["hit"] == "1")
        p(f"| {c} | {len(rs)} ({pct(len(rs), len(rifle))}) | {mean([float(r['final_chance']) for r in rs]):.1f}% | {pct(h, len(rs))} |")

    p("\n### By target stance / movement\n")
    p("| Target | Shots | Mean chance | Hit rate |")
    p("|---|---|---|---|")
    by_stance = defaultdict(list)
    for r in rifle:
        by_stance[r["target_stance"] + (" (moving)" if r["target_moving"] == "1" else "")].append(r)
    for k, rs in sorted(by_stance.items(), key=lambda kv: -len(kv[1])):
        h = sum(1 for r in rs if r["hit"] == "1")
        p(f"| {k} | {len(rs)} | {mean([float(r['final_chance']) for r in rs]):.1f}% | {pct(h, len(rs))} |")

    p("\n### By weapon\n")
    p("| Weapon | Shots | Mean chance | Hit rate | Kills (all causes, by soldiers holding it) |")
    p("|---|---|---|---|---|")
    by_weapon = defaultdict(list)
    for r in rifle:
        by_weapon[r["shooter_weapon"]].append(r)
    kills_by_weapon = Counter()
    for m in matches:
        for s in m["soldiers"]:
            kills_by_weapon[s["weapon"]] += s["kills"]
    for w, rs in sorted(by_weapon.items()):
        h = sum(1 for r in rs if r["hit"] == "1")
        p(f"| {w} | {len(rs)} | {mean([float(r['final_chance']) for r in rs]):.1f}% | {pct(h, len(rs))} | {kills_by_weapon[w]} |")

    # ---- Grenades
    p("\n## Grenades\n")
    thrown = sum(t["grenades_thrown"] for m in matches for t in m["teams"])
    gk = sum(t["blast_kills"] for m in matches for t in m["teams"])
    gs = sum(t["blast_stuns"] for m in matches for t in m["teams"])
    ff = sum(t["friendly_blast_casualties"] for m in matches for t in m["teams"])
    carried = sum(5 for m in matches for _ in m["soldiers"])
    p(f"- {thrown} thrown of {carried} carried ({pct(thrown, carried)}); {mean([thrown / n]):.1f} per match.")
    p(f"- Enemy kills {gk}, enemy stuns {gs}, friendly casualties (kills+stuns) {ff}.")
    p(f"- Per grenade: {gk / thrown:.2f} kills, {gs / thrown:.2f} stuns." if thrown else "- No grenades thrown.")
    dists = [float(e.split("throws ")[1].rstrip("m")) for m in matches for e in m["events"] if " throws " in e]
    if dists:
        p(f"- Throw distance: mean {mean(dists):.1f} m, max {max(dists):.1f} m.")

    # ---- Medkits, stress, VPs
    p("\n## Medkits\n")
    injured = sum(1 for r in hits if r["outcome"] == "injured")
    treated = sum(s["medkits_given"] for m in matches for s in m["soldiers"])
    p(f"- {injured} injuries from rifle hits; {treated} treated with a medkit ({pct(treated, injured)}).")
    end_injured = sum(1 for m in matches for s in m["soldiers"] if s["end_state"] == "injured")
    p(f"- {end_injured} soldiers were still lying injured when their match ended.")

    p("\n## Stress\n")
    tiers = [0.0] * 4
    peaks = []
    for m in matches:
        for s in m["soldiers"]:
            for i, v in enumerate(s["stress_tier_seconds"]):
                tiers[i] += v
            peaks.append(s["peak_stress"])
    alive_time = sum(tiers)
    p("- Time alive by stress tier: " + ", ".join(
        f"{name} {pct(t, alive_time)}" for name, t in zip(("none (<33)", "minor (33-66)", "major (66-99)", "shocked (100)"), tiers)) + ".")
    p(f"- Peak stress per soldier: mean {mean(peaks):.1f}, max {max(peaks):.1f}; "
      f"{sum(1 for x in peaks if x >= 33)} of {len(peaks)} soldiers ever reached 33.")
    stressed_shots = [r for r in rifle if float(r["stress_mult"]) < 0.999]
    p(f"- Shots fired with a stress accuracy penalty: {len(stressed_shots)} of {len(rifle)}.")

    p("\n## Victory points\n")
    caps = Counter()
    for m in matches:
        for e in m["events"]:
            if " capture " in e:
                caps[e.split(" capture ")[1].split(" by ")[0]] += 1
    p("- Captures per VP: " + ", ".join(f"{k} {v}" for k, v in sorted(caps.items())) + f" ({sum(caps.values()) / n:.1f} per match).")
    first_caps = [float(e.split()[0]) for m in matches for e in m["events"] if " capture " in e]
    if first_caps:
        p(f"- First capture at {min(first_caps):.1f} s.")

    text = "\n".join(out) + "\n"
    if a.out:
        with open(a.out, "w", encoding="utf-8") as fh:
            fh.write(text)
    print(text)


if __name__ == "__main__":
    main()
