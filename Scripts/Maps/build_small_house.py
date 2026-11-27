"""Builds /Game/Maps/SmallHouse, the "Small House" map (GDD §10.6).

A 44x44 map: a ruined house of wooden stockade walls (full walls) in the middle, with two VPs inside it (Beds, 8 points
a minute; Main, 6), and a small stone-walled yard (low walls) in front of each side's deployment zone holding that
side's home VP (3 points a minute). Adapted from a screenshot of an older game's map; one of its map tiles is 3 cells.

The Axis deploy in the corner at cells (1-8, 1-8), the Allies in the opposite corner (35-42, 35-42). The map is mirrored
across the diagonal between the other two corners: the cover on cell (X, Y) is the same as on (43 - Y, 43 - X), so
every VP's distance to both DZs is the same. (The game's view shows world -X to the right of +Y, so with the Axis DZ at
the bottom of the screen Beds is on the left and Main on the right, as in the source screenshot.)

The PLAN below is the record of the layout (row y = 43 at the top, x = 0..43 left to right):
    #  wooden stockade wall (AWallCover with the SM_Wall_Wood meshes)   s  stone low wall (ALowWallCover)
    w  window in a wooden wall (AWindowWallCover "#w#" piece, SM_Wall_Wood_Window)
    h  hay bale   T  tree   .  open ground
Walls are built from straight runs as in build_farmhouse_b.py: a wall cell with a wall to its east or west belongs to
a run along X, every other wall cell to a run along Y. Runs along X own the junction cells and are trimmed to the
crossing wall's face; runs along Y are lengthened to meet the X wall's face.

Zones (GDD §10.6): Axis DZ 1-8 x 1-8, Allied DZ 35-42 x 35-42; VPs (5x5) Axis 9-13 x 10-14, Allies 29-33 x 30-34,
Beds 25-29 x 14-18, Main 13-17 x 26-30.

Run in the full editor (actor spawning in the headless commandlet is unreliable):
    UnrealEditor.exe Unreal.uproject /Game/Maps/Farmstead -ExecCmds="py C:/.../Scripts/Maps/build_small_house.py"
The first run starts from Farmstead (lighting, grid, squads, DZs), clears its layout and saves the result as
/Game/Maps/SmallHouse. Later runs open SmallHouse, clear everything this script places and rebuild it. It saves the
level, writes Saved/MapAudit/build_small_house.txt and quits the editor (unless SMALLHOUSE_STAY=1).
"""
import os

import unreal

MAP = "/Game/Maps/SmallHouse"
TEMPLATE = "/Game/Maps/Farmstead"
REPORT = unreal.Paths.project_saved_dir() + "MapAudit/build_small_house.txt"
N = 44
HALF = (N - 1) * 50.0  # cell (x, y) centre = (x * 100 - HALF, y * 100 - HALF)
FOLDER = "SmallHouse"
ACT = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
WOOD_MESHES = ["/Game/Environment/Walls/SM_Wall_Wood", "/Game/Environment/Walls/SM_Wall_Wood_B"]
WOOD_WINDOW_MESH = "/Game/Environment/Walls/SM_Wall_Wood_Window"

PLAN_ROWS = [
    "............................................",  # y=43
    "............................................",  # y=42
    "...........T................................",  # y=41
    "............................................",  # y=40
    "............................................",  # y=39
    ".....T......................T...............",  # y=38
    "............................................",  # y=37
    "............................................",  # y=36
    "............................sssssss.........",  # y=35  Allied yard (VP Allies 29-33 x 30-34)
    ".........T........#######.........s.........",  # y=34
    "..................#.....#.........s.........",  # y=33
    "..T...............#.....#...s.....s.........",  # y=32
    "............#######.....#...s...............",  # y=31
    "............#.....#.........s...............",  # y=30  Main room (VP Main 13-17 x 26-30)
    "............#.....#.........sssssss.........",  # y=29
    "............#....##.....#...................",  # y=28
    "............#...........#...................",  # y=27
    "............#..#........#...................",  # y=26
    ".........#######..#######..#######..........",  # y=25
    ".........#........#.....#..#.....#..........",  # y=24
    ".........#........#.....#........#..........",  # y=23
    ".........#........#.....#........#..........",  # y=22
    ".........#........#..............#..........",  # y=21
    ".........#........#..............#..........",  # y=20
    ".........####..#######..#######..#..........",  # y=19
    "........................#.....#..#..........",  # y=18  Bedroom (VP Beds 25-29 x 14-18)
    "........................#........#..........",  # y=17
    "..................##....#........#..h.......",  # y=16
    ".....T..s..ssss...#.....#........w..........",  # y=15
    "........s.....s...#.....#.....#..#..........",  # y=14  Axis yard (VP Axis 9-13 x 10-14)
    "........s.....s...#.....##...##..#....h.....",  # y=13
    "........s.....s...#..............#..........",  # y=12
    "........s.....s...#..............#..........",  # y=11
    "........s.....s...##########w#####..........",  # y=10
    "........ssss..s.............................",  # y=9
    "........................................h...",  # y=8
    "...........................h................",  # y=7
    "............................................",  # y=6
    "..............................h.............",  # y=5
    "............................................",  # y=4
    "...................................h........",  # y=3
    "............................................",  # y=2
    "............................................",  # y=1
    "............................................",  # y=0
]

# (label, PointName, min cell, size, points per minute)
VPS = [
    ("VP_Axis", "Axis", (9, 10), 5, 3.0),
    ("VP_Allies", "Allies", (29, 30), 5, 3.0),
    ("VP_Beds", "Beds", (25, 14), 5, 8.0),
    ("VP_Main", "Main", (13, 26), 5, 6.0),
]
DZS = {unreal.SquadTeam.AXIS: (1, 1), unreal.SquadTeam.ALLIES: (35, 35)}  # min cell, 8x8
# Squads start along their DZ's diagonal, facing the house (yaw), in SquadIndex order.
SQUAD_CELLS = {unreal.SquadTeam.AXIS: ([(7, 7), (6, 6), (5, 5), (4, 4)], 45.0),
               unreal.SquadTeam.ALLIES: ([(36, 36), (37, 37), (38, 38), (39, 39)], -135.0)}
PLAYER_START = (39, 39)

PLAN = {}
for row, line in enumerate(PLAN_ROWS):
    assert len(line) == N, (row, line)
    for x, ch in enumerate(line):
        if ch != ".":
            PLAN[(x, N - 1 - row)] = ch
# Wall families: the plan characters that make one run of wall.
FAMILIES = {"#": "#w", "s": "s"}
# Trim / lengthen at junctions = 50 cm (half a cell) - half the crossing wall's thickness.
JOINT = {"#": 35.0, "s": 30.0}

report = []


def world(x, y, z=0.0):
    return unreal.Vector(x * 100.0 - HALF, y * 100.0 - HALF, z)


def cells(actor):
    return {(c.x, c.y) for c in actor.get_occupied_cells()}


def check_symmetry():
    bad = [(c, ch) for c, ch in PLAN.items() if PLAN.get((N - 1 - c[1], N - 1 - c[0])) != ch]
    for c, ch in bad:
        report.append("ASYMMETRIC %s %r" % (c, ch))
    return len(bad)


def runs(ch):
    walls = {c for c, k in PLAN.items() if k in FAMILIES[ch]}
    along_x = {c for c in walls if (c[0] - 1, c[1]) in walls or (c[0] + 1, c[1]) in walls}
    out = []
    for axis, group in (("x", along_x), ("y", walls - along_x)):
        lines = {}
        for (x, y) in group:
            line, v = (y, x) if axis == "x" else (x, y)
            lines.setdefault(line, []).append(v)
        for line, vs in sorted(lines.items()):
            vs.sort()
            run = [vs[0]]
            for v in vs[1:]:
                if v != run[-1] + 1:
                    out.append((axis, line, run))
                    run = []
                run.append(v)
            out.append((axis, line, run))
    return out, walls, along_x


def cell_of(axis, line, v):
    return (v, line) if axis == "x" else (line, v)


def end_extension(ch, axis, line, v, step, walls, along_x):
    if axis == "x":
        x, y = cell_of(axis, line, v)
        return -JOINT[ch] if ((x, y - 1) in walls or (x, y + 1) in walls) else 0.0
    nxt = cell_of(axis, line, v + step)
    if nxt in along_x:
        return JOINT[ch]
    if nxt in walls:
        raise RuntimeError("Y run meets another Y run at %s" % (nxt,))
    return 0.0


def split(chars):
    """Pieces of one wooden run: (kind, first index, last index); a window cell takes a wall cell either side."""
    out, i, n = [], 0, len(chars)
    while i < n:
        if i + 2 < n and chars[i + 1] == "w":
            if chars[i] != "#" or chars[i + 2] != "#":
                raise RuntimeError("window needs a wall cell either side in %r" % chars)
            out.append(("window", i, i + 2))
            i += 3
            continue
        if chars[i] == "w":
            raise RuntimeError("window needs a wall cell either side in %r" % chars)
        j = i
        while j + 1 < n and chars[j + 1] == chars[i] and not (j + 3 < n and chars[j + 2] == "w"):
            j += 1
        out.append(("wall" if chars[i] == "#" else "low", i, j))
        i = j + 1
    return out


CLASSES = {"wall": unreal.WallCover, "window": unreal.WindowWallCover, "low": unreal.LowWallCover}


def place_run(kind, axis, line, first, last, ext_lo, ext_hi, wood):
    want = {cell_of(axis, line, v) for v in range(first, last + 1)}
    piece = ACT.spawn_actor_from_class(CLASSES[kind], world(*cell_of(axis, line, first)), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    if kind in ("wall", "window"):
        piece.set_editor_property("WallMeshes", wood["wall"])
    if kind == "window":
        piece.set_editor_property("WindowMeshes", wood["window"])
        piece.set_editor_property("WindowSpacing", 2)
    piece.set_editor_property("Length", last - first + 1)
    piece.set_folder_path(FOLDER + ("/LowWalls" if kind == "low" else "/Walls"))
    piece.set_actor_label("SH_%s_%s%d_%d-%d" % (kind, axis, line, first, last))
    mid = (first + last) // 2
    yaw = 0.0 if axis == "x" else 90.0
    for d in (0, 1, -1):
        for turn in (yaw, yaw + 180.0):
            cx, cy = cell_of(axis, line, mid + d)
            piece.set_actor_location_and_rotation(world(cx, cy), unreal.Rotator(roll=0.0, pitch=0.0, yaw=turn), False, False)
            if cells(piece) == want:
                lo_is_start = turn == yaw  # Local +X points up the run unless turned round.
                piece.set_editor_property("StartExtension", ext_lo if lo_is_start else ext_hi)
                piece.set_editor_property("EndExtension", ext_hi if lo_is_start else ext_lo)
                return 0
    report.append("MISMATCH %s: %s vs %s" % (piece.get_actor_label(), sorted(cells(piece)), sorted(want)))
    return 1


def build_walls(wood):
    bad = pieces = 0
    for ch in FAMILIES:
        wall_runs, walls, along_x = runs(ch)
        for axis, line, vs in wall_runs:
            lo = end_extension(ch, axis, line, vs[0], -1, walls, along_x)
            hi = end_extension(ch, axis, line, vs[-1], 1, walls, along_x)
            parts = split("".join(PLAN[cell_of(axis, line, v)] for v in vs))
            for k, (kind, i, j) in enumerate(parts):
                bad += place_run(kind, axis, line, vs[i], vs[j], lo if k == 0 else 0.0, hi if k == len(parts) - 1 else 0.0, wood)
                pieces += 1
    report.append("walls: %d pieces, %d mismatched" % (pieces, bad))
    return bad


def build_singles():
    bad = 0
    for (x, y), ch in sorted(PLAN.items()):
        cls = {"h": unreal.HayBaleCover, "T": unreal.TreeCover}.get(ch)
        if not cls:
            continue
        a = ACT.spawn_actor_from_class(cls, world(x, y), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
        a.set_editor_property("Length", 1)  # Hay bales default to a 2-cell row.
        a.set_folder_path(FOLDER + ("/Hay" if ch == "h" else "/Trees"))
        a.set_actor_label("SH_%s_%d_%d" % ("hay" if ch == "h" else "tree", x, y))
        if cells(a) != {(x, y)}:
            report.append("SINGLE MISMATCH %s at %s: %s" % (ch, (x, y), sorted(cells(a))))
            bad += 1
    return bad


def zone_centre(min_cell, size):
    return world(min_cell[0] + (size - 1) / 2.0, min_cell[1] + (size - 1) / 2.0)


def build_zones(actors):
    bad = 0
    for label, name, mn, size, ppm in VPS:
        vp = ACT.spawn_actor_from_class(unreal.VictoryPoint, zone_centre(mn, size), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
        vp.set_actor_label(label)
        vp.set_folder_path(FOLDER + "/Zones")
        vp.set_editor_property("SizeX", size)
        vp.set_editor_property("SizeY", size)
        vp.set_editor_property("PointName", unreal.Text(name))
        vp.set_editor_property("PointsPerMinute", ppm)
        got = vp.get_min_cell()
        if (got.x, got.y) != mn:
            report.append("VP %s min cell %s, wanted %s" % (label, (got.x, got.y), mn))
            bad += 1
    dzs = [a for a in actors if isinstance(a, unreal.DeploymentZone)]
    for dz in dzs:
        team = dz.get_editor_property("Team")
        mn = DZS[team]
        dz.set_editor_property("SizeX", 8)
        dz.set_editor_property("SizeY", 8)
        dz.set_actor_location(zone_centre(mn, 8), False, False)
        dz.set_folder_path(FOLDER + "/Zones")
        got = dz.get_min_cell()
        if (got.x, got.y) != mn:
            report.append("DZ %s min cell %s, wanted %s" % (team, (got.x, got.y), mn))
            bad += 1
    if len(dzs) != 2:
        report.append("expected 2 DeploymentZones, found %d" % len(dzs))
        bad += 1
    return bad


def place_squads(actors):
    bad = 0
    soldiers = sorted([a for a in actors if isinstance(a, unreal.Soldier)], key=lambda a: (a.get_editor_property("SquadIndex"), a.get_actor_label()))
    used = {t: 0 for t in SQUAD_CELLS}
    for s in soldiers:
        team = s.get_editor_property("Team")
        spots, yaw = SQUAD_CELLS[team]
        if used[team] >= len(spots):
            report.append("extra soldier %s left where it is" % s.get_actor_label())
            bad += 1
            continue
        z = s.get_actor_location().z
        s.set_actor_location_and_rotation(world(*spots[used[team]], z), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw), False, False)
        used[team] += 1
    report.append("soldiers placed: %s" % {str(t): n for t, n in used.items()})
    for a in actors:
        if isinstance(a, unreal.PlayerStart):
            a.set_actor_location_and_rotation(world(*PLAYER_START, a.get_actor_location().z), unreal.Rotator(roll=0.0, pitch=0.0, yaw=-135.0), False, False)
    return bad


def clear(actors):
    removed = 0
    for a in actors:
        folder = str(a.get_folder_path())
        if isinstance(a, (unreal.CoverActor, unreal.VictoryPoint)) or (
                isinstance(a, unreal.StaticMeshActor) and folder.startswith(("Farmstead/", FOLDER + "/"))):
            ACT.destroy_actor(a)
            removed += 1
    report.append("cleared %d actors" % removed)


def audit(world_obj):
    """Every plan cell is covered by exactly the right kind of piece, and nothing else is."""
    want = {c: ("#" if ch == "w" else ch) for c, ch in PLAN.items()}
    kind = {unreal.WallCover: "#", unreal.WindowWallCover: "#", unreal.LowWallCover: "s", unreal.HayBaleCover: "h", unreal.TreeCover: "T"}
    got = {}
    for a in unreal.GameplayStatics.get_all_actors_of_class(world_obj, unreal.CoverActor):
        ch = next((v for k, v in kind.items() if type(a) is k), "?")
        for c in cells(a):
            if c in got:
                report.append("OVERLAP at %s" % (c,))
            got[c] = ch
    diff = [(c, want.get(c), got.get(c)) for c in set(want) | set(got) if want.get(c) != got.get(c)]
    for d in sorted(diff, key=lambda t: t[0]):
        report.append("AUDIT %s want %r got %r" % d)
    report.append("audit: %d cells, %d differences" % (len(want), len(diff)))
    return len(diff)


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    first = not unreal.EditorAssetLibrary.does_asset_exist(MAP)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    current = ues.get_editor_world().get_path_name().split(".")[0]
    want_open = TEMPLATE if first else MAP
    if current != want_open:
        report.append("ABORT: start the editor on %s (got %s)" % (want_open, current))
        return
    bad = check_symmetry()
    wood = {"wall": [unreal.load_asset(p) for p in WOOD_MESHES], "window": [unreal.load_asset(WOOD_WINDOW_MESH)]}
    if not all(wood["wall"] + wood["window"]):
        report.append("ABORT: missing wood wall meshes %s (run SourceArt/Environment/import_cover_unreal.py)" % (WOOD_MESHES + [WOOD_WINDOW_MESH]))
        return
    actors = ACT.get_all_level_actors()
    clear(actors)
    grid = next(a for a in ACT.get_all_level_actors() if isinstance(a, unreal.GridManager))
    grid.set_actor_location(unreal.Vector(0, 0, 0), False, False)
    grid.set_editor_property("GridSizeX", N)
    grid.set_editor_property("GridSizeY", N)
    grid.set_editor_property("MatchTimeLimit", 0.0)
    actors = ACT.get_all_level_actors()
    bad += build_walls(wood)
    bad += build_singles()
    bad += build_zones(actors)
    bad += place_squads(actors)
    w = ues.get_editor_world()
    bad += audit(w)
    if bad:
        report.append("NOT SAVED: %d problems" % bad)
        return
    if first:
        ok = unreal.EditorLoadingAndSavingUtils.save_map(w, MAP)
    else:
        path = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir() + "Maps/SmallHouse.umap")
        if os.path.exists(path):
            os.chmod(path, 0o666)  # Git LFS checks lockable maps out read-only.
        ok = les.save_current_level()
    report.append(("saved %s" if ok else "SAVE FAILED for %s") % MAP)


try:
    main()
except Exception as e:  # Report instead of leaving the editor open with no output.
    import traceback
    report.append("ERROR %r\n%s" % (e, traceback.format_exc()))
os.makedirs(os.path.dirname(REPORT), exist_ok=True)
open(REPORT, "w").write("\n".join(report) + "\n")
if os.environ.get("SMALLHOUSE_STAY") != "1":
    unreal.SystemLibrary.quit_editor()
