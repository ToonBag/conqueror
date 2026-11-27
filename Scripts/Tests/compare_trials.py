"""One row per AI-match trial, for tempo and stress tuning (GDD §8, §11.2).

Each trial is a pair of record folders: <name> (Allies start south) and <name>_swap (Axis start south),
as written by AIMatchBatch.ps1 and moved into Saved/MatchStats/<name>/. Run with the engine's bundled Python:
    python.exe -I Scripts/Tests/compare_trials.py Saved/MatchStats base_rolls r_mov r_settle ...
"""
import csv, glob, json, os, statistics, sys


def load(folder):
    matches, shots = [], []
    for f in sorted(glob.glob(os.path.join(folder, "*.json"))):
        matches.append(json.load(open(f, encoding="utf-8")))
    for f in sorted(glob.glob(os.path.join(folder, "*_shots.csv"))):
        with open(f, encoding="utf-8") as fh:
            shots += list(csv.DictReader(fh))
    return matches, shots


def row(root, name):
    allies = axis = south = north = timeouts = 0
    matches, shots = [], []
    for suffix, south_team in (("", "Allies"), ("_swap", "Axis")):
        m, s = load(os.path.join(root, name + suffix))
        matches += m
        shots += s
        for j in m:
            w = j.get("winner", "")
            if w == "Allies": allies += 1
            elif w == "Axis": axis += 1
            else: timeouts += 1
            if w in ("Allies", "Axis"): south, north = (south + 1, north) if w == south_team else (south, north + 1)
    if not matches:
        return None
    soldiers = [s for j in matches for s in j["soldiers"]]
    deaths = sorted(s["death_time"] for s in soldiers if s["death_time"] >= 0)
    first = []
    for j in matches:
        d = [s["death_time"] for s in j["soldiers"] if s["death_time"] >= 0]
        if d: first.append(min(d))
    rifle = [r for r in shots if r.get("shooter_weapon") in ("M1 Garand", "Gewehr 43")]
    moving = [r for r in rifle if r["target_moving"] == "1"]
    stressed = [r for r in rifle if float(r["stress_mult"]) < 0.999]
    hits = sum(int(r["hit"]) for r in rifle)
    tiers = [sum(s["stress_tier_seconds"][i] for s in soldiers) for i in range(4)]
    alive = sum(tiers) or 1
    grenades = sum(t["grenades_thrown"] for j in matches for t in j["teams"])
    medkits = sum(s["medkits_given"] for s in soldiers)
    return {
        "trial": name, "n": len(matches),
        "Al-Ax": "%d-%d" % (allies, axis), "S-N": "%d-%d" % (south, north), "t/o": timeouts,
        "len min": "%.1f" % statistics.mean(j["duration_s"] / 60 for j in matches),
        "1st death s": "%.0f" % statistics.median(first) if first else "-",
        "half deaths s": "%.0f" % deaths[len(deaths) // 2] if deaths else "-",
        "deaths >60s": "%.0f%%" % (100 * sum(d > 60 for d in deaths) / max(1, len(deaths))),
        "rds/soldier": "%.1f" % (len(rifle) / len(soldiers)),
        "hit%": "%.0f" % (100 * hits / max(1, len(rifle))),
        "at movers%": "%.0f" % (100 * len(moving) / max(1, len(rifle))),
        "reached 33": "%.0f%%" % (100 * sum(s["peak_stress"] >= 33 for s in soldiers) / len(soldiers)),
        "reached 66": "%.0f%%" % (100 * sum(s["peak_stress"] >= 66 for s in soldiers) / len(soldiers)),
        "time >=Minor": "%.1f%%" % (100 * (alive - tiers[0]) / alive),
        "stressed shots": "%.0f%%" % (100 * len(stressed) / max(1, len(rifle))),
        "grenades/match": "%.1f" % (grenades / len(matches)),
        "medkits/match": "%.1f" % (medkits / len(matches)),
    }


def main():
    root, names = sys.argv[1], sys.argv[2:]
    rows = [r for r in (row(root, n) for n in names) if r]
    cols = list(rows[0].keys())
    print("| " + " | ".join(cols) + " |")
    print("|" + "---|" * len(cols))
    for r in rows:
        print("| " + " | ".join(str(r[c]) for c in cols) + " |")


main()
