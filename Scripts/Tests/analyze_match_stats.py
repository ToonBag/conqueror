"""Summarise AI test-match records (Saved/MatchStats/*.json + *_shots.csv) into balancing tables.

Run with any Python 3 (UE ships one):
  "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" -I \
      Scripts/Tests/analyze_match_stats.py [Saved/MatchStats] [--since 20261123-1550] [--out report.md]

Writes Markdown to stdout (or --out). Records come from Scripts/AIMatchBatch.ps1 (squad.MatchStats 1).
The Rushes section needs the _track.csv position samples (records from GDD v0.62 on).
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

# Rush detection (GDD §11.2), from the _track.csv position samples.
RUSH_FAR_M = 15.0        # an approach starts from further than this from the nearest active enemy
RUSH_CLOSE_M = 10.0      # ... and ends once within this
RUSH_MAX_S = 15.0        # ... taking at most this long
RUSH_MOVING_FRAC = 0.6   # a rush moves for at least this share of the approach
RUSH_SPOTTED_FRAC = 0.5  # ... and is seen by the enemy for at least this share (open ground)
RUSH_RESULT_S = 20.0     # after arriving, how long it has to injure or kill an enemy


def find_approaches(track, shots, events):
    """Approaches into close range, per soldier. Returns dicts: soldier, team, kind ("rush" / "covered"),
    seconds, result ("won" / "lost" / "none"), traded (went down after winning, inside the window)."""
    by_time = defaultdict(list)
    for r in track:
        by_time[r["time"]].append(r)
    times = sorted(by_time, key=float)
    # Per soldier, its samples in time order with the distance to the nearest active enemy.
    per = defaultdict(list)
    pos = {}  # (time, soldier) -> (x, y)
    for t in times:
        rows = by_time[t]
        for r in rows:
            pos[(t, r["soldier"])] = (int(r["x"]), int(r["y"]))
        for r in rows:
            x, y = pos[(t, r["soldier"])]
            ex = [(((x - int(e["x"])) ** 2 + (y - int(e["y"])) ** 2) ** 0.5, e["soldier"]) for e in rows
                  if e["team"] != r["team"] and e["state"] == "active"]
            d, nearest = min(ex) if ex else (1e9, None)
            r["_t"], r["_nearest"] = t, nearest
            per[r["soldier"]].append((float(t), d, r))
    dist = lambda a, b: ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5
    # Scoring: rifle injuries/kills (shots) and grenade/C4 kills (death events) by each soldier.
    scored = defaultdict(list)
    for s in shots:
        if s.get("outcome") in ("injured", "killed"):
            scored[s["shooter"]].append(float(s["time"]))
    for e in events:
        parts = e.split()
        if len(parts) >= 5 and parts[1] == "death" and parts[3] == "by" and "rifle" not in e and "bled_out" not in e:
            scored[parts[4]].append(float(parts[0]))
    out = []
    for sid, samples in per.items():
        last_seen = samples[-1][0]
        last_far = None
        armed = True  # one approach per trip in; re-armed once it is far from every enemy again
        for i, (t, d, r) in enumerate(samples):
            if r["state"] != "active":
                last_far = None  # an approach doesn't run through being injured or stunned
                continue
            if d > RUSH_FAR_M:
                last_far, armed = i, True
            elif d <= RUSH_CLOSE_M and armed and last_far is not None and t - samples[last_far][0] <= RUSH_MAX_S:
                armed = False
                window = samples[last_far:i + 1]
                # Who closed the gap? Measured against the enemy it ends up nearest: the soldier's own move towards
                # where that enemy ended, against that enemy's move towards where the soldier ended. Skipped when
                # the enemy did more of it (being walked up to is not a rush).
                t0, enemy = window[0][2]["_t"], r["_nearest"]
                e0, e1 = pos.get((t0, enemy)), pos.get((r["_t"], enemy))
                s0, s1 = pos[(t0, sid)], pos[(r["_t"], sid)]
                if e0 and e1 and dist(s0, e1) - dist(s1, e1) < dist(s1, e0) - dist(s1, e1):
                    continue
                moving = sum(w[2]["moving"] == "1" for w in window) / len(window)
                spotted = sum(w[2]["spotted"] == "1" for w in window) / len(window)
                kind = "rush" if moving >= RUSH_MOVING_FRAC and spotted >= RUSH_SPOTTED_FRAC else "covered"
                start, end = samples[last_far][0], t + RUSH_RESULT_S
                # Down = not active any more, or gone from the track (dead) before the match's last sample.
                down = next((u for u, _, w in samples[i + 1:] if w["state"] != "active"), None)
                if down is None and last_seen < float(times[-1]):
                    down = last_seen + 0.5
                won = next((s for s in sorted(scored[sid]) if start <= s <= end), None)
                if won is not None and (down is None or won <= down):
                    result = "won"
                elif down is not None and down <= end:
                    result = "lost"
                else:
                    result = "none"
                traded = result == "won" and down is not None and down <= end
                out.append({"soldier": sid, "team": r["team"], "kind": kind, "seconds": t - start,
                            "result": result, "traded": traded})
    return out


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
        m["_shots"] = []
        csv_path = f[:-5] + "_shots.csv"
        if os.path.exists(csv_path):
            with open(csv_path, encoding="utf-8") as fh:
                for r in csv.DictReader(fh):
                    r["seed"] = m["seed"]
                    shots.append(r)
                    m["_shots"].append(r)
        track_path = f[:-5] + "_track.csv"  # records before GDD v0.62 have none
        m["_track"] = None
        if os.path.exists(track_path):
            with open(track_path, encoding="utf-8") as fh:
                m["_track"] = list(csv.DictReader(fh))

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
    p("| Seed | Winner | How | Length | Allies alive | Axis alive | Allies injured | Axis injured | VPs at end | Score (Al-Ax) |")
    p("|---|---|---|---|---|---|---|---|---|---|")
    for m in matches:
        alive = Counter(); inj = Counter()
        for s in m["soldiers"]:
            if s["end_state"] != "dead":
                alive[s["team"]] += 1
            if s["end_state"] == "injured":
                inj[s["team"]] += 1
        vps = " ".join(f"{k}:{v[:2]}" for k, v in sorted(m["vp_owners"].items()))
        score = m.get("score")  # GDD §11 points; older records have none
        pts = f"{score['Allies']}-{score['Axis']}" if score else "-"
        p(f"| {m['seed']} | {m['winner']} | {m['reason']} | {m['duration_s'] / 60:.1f} min | {alive['Allies']} | {alive['Axis']} "
          f"| {inj['Allies']} | {inj['Axis']} | {vps} | {pts} |")
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

    # ---- Rushes
    tracked = [m for m in matches if m["_track"]]
    p("\n## Rushes\n")
    if not tracked:
        p("- No position tracks in these records (recorded before GDD v0.62).")
    else:
        apps = [a for m in tracked for a in find_approaches(m["_track"], m["_shots"], m["events"])]
        p(f"An approach is a soldier closing from over {RUSH_FAR_M:.0f} m to within {RUSH_CLOSE_M:.0f} m of the nearest active enemy "
          f"in {RUSH_MAX_S:.0f} s or less, having done at least half the closing itself (being walked up to doesn't count). It is a **rush** if the soldier was moving for {RUSH_MOVING_FRAC:.0%} of it and seen by the "
          f"enemy for {RUSH_SPOTTED_FRAC:.0%} of it, otherwise a **covered** approach. **Won**: it injured or killed an enemy (rifle, "
          f"grenade or C4 kill) from the start of the approach to {RUSH_RESULT_S:.0f} s after arriving, before going down itself. "
          f"**Lost**: it was injured, stunned or killed first. **Traded**: won, then went down inside the window. "
          f"Grenade stuns are not credited. {len(tracked)} of {n} matches have tracks.\n")
        p("| Approach | Side | Count | Per match | Won | Lost | Neither | Traded | Mean time 15 to 10 m |")
        p("|---|---|---|---|---|---|---|---|---|")
        for kind in ("rush", "covered"):
            for side in ("All", "Allies", "Axis"):
                xs = [a for a in apps if a["kind"] == kind and side in ("All", a["team"])]
                if not xs:
                    continue
                res = Counter(a["result"] for a in xs)
                p(f"| {kind} | {side} | {len(xs)} | {len(xs) / len(tracked):.1f} | {pct(res['won'], len(xs))} | {pct(res['lost'], len(xs))} "
                  f"| {pct(res['none'], len(xs))} | {pct(sum(a['traded'] for a in xs), len(xs))} | {mean([a['seconds'] for a in xs]):.1f} s |")
        rush = [a for a in apps if a["kind"] == "rush"]
        if rush:
            won = sum(a["result"] == "won" for a in rush)
            lost = sum(a["result"] == "lost" for a in rush)
            p(f"\n- Rush success (won / (won + lost)): {pct(won, won + lost)}. Above 50% means charging across open ground pays.")

    text = "\n".join(out) + "\n"
    if a.out:
        with open(a.out, "w", encoding="utf-8") as fh:
            fh.write(text)
    print(text)


if __name__ == "__main__":
    main()
