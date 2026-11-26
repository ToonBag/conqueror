"""Builds Farmstead's VP B farmhouse and fixes wall corners (GDD §10.5, v0.59).

VP B sits in the walled keep of a 16x16-cell farmhouse (cells 25-40 x 24-39). From either side you reach the keep
through a foyer and a corner room, then a side corridor to one of the keep's two side doorways; the keep's wall
facing each foyer has windows and a weakened (breachable) cell. The plan below is the record of the layout. Its
north half is the mirror of its south half (cell (x, y) == (x, 63 - y)), like the rest of the map.

Walls are built from straight runs. A cell with a wall to its east or west belongs to a run along X; every other wall
cell belongs to a run along Y. Runs along X own the junction cells: an X run ending at a junction is trimmed by 35 cm
so its end is flush with the crossing wall's outer face, and a Y run ending against an X wall is lengthened by 35 cm to
meet its face. Each run is split into WallCover, WindowWallCover ("#w#", "#w#w#" ...) and BreachableWall pieces.

Also fixes the C room's corners: its weakened walls at (56, 28) / (56, 35) stopped 35 cm short of the corner post.

Run in the full editor with Farmstead open (actor duplication crashes the headless commandlet):
    UnrealEditor.exe Unreal.uproject /Game/Maps/Farmstead -ExecCmds="py C:/.../Scripts/Maps/build_farmhouse_b.py"
It saves the level and quits the editor. Writes a report to Saved/MapAudit/build_farmhouse_b.txt. Running it twice is
harmless: everything inside the building's footprint is removed and rebuilt.
"""
import math
import unreal

LEVEL = "/Game/Maps/Farmstead"
REPORT = unreal.Paths.project_saved_dir() + "MapAudit/build_farmhouse_b.txt"
ACT = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
FOLDER = "Farmstead/Cover/FarmhouseB"

# South half of the farmhouse, row y = 24 (outer south wall) up to y = 31; x runs 25..40 left to right.
#   # wall   w window   B weakened wall   . floor / doorway   c crate stack   b barrels
X0, Y0 = 25, 24
SOUTH_HALF = [
    "##w##w#..#w##w##",  # y=24  outer wall: front door 32-33 into the foyer, windows
    "#...#c....c#...#",  # y=25  corner rooms (26-28, 37-39) either side of the foyer (30-35)
    "................",  # y=26  side doors (25, 40) into the corner rooms, doorways (29, 36) foyer <-> corner rooms
    "#...#......#...#",  # y=27
    "#.###w#B#w####.#",  # y=28  keep's south wall (29-36): windows + weakened cell 32 facing the foyer; doorways 26 / 39
    "#...#......#...#",  # y=29  side corridors (26-28, 37-39) | keep = VP B (30-35 x 29-34)
    "w.#.#.c..b.#.#.w",  # y=30  baffle walls in the corridors (27, 38), cover in the keep
    "#.#..........#.#",  # y=31  keep doorways (29, 36) at rows 31-32
]
PLAN = {}
for dy, row in enumerate(SOUTH_HALF):
    assert len(row) == 16, row
    for dx, ch in enumerate(row):
        PLAN[(X0 + dx, Y0 + dy)] = ch
        PLAN[(X0 + dx, 63 - (Y0 + dy))] = ch
X1, Y1 = X0 + 15, 63 - Y0  # 40, 39

WALL_CHARS = "#wB"
PROPS = {"c": unreal.CrateStackCover, "b": unreal.BarrelCover}

report = []


def cells(actor):
    return {(c.x, c.y) for c in actor.get_occupied_cells()}


def world(x, y, z=0.0):
    return unreal.Vector(x * 100.0 - 3150.0, y * 100.0 - 3150.0, z)


def in_footprint(c):
    return X0 <= c[0] <= X1 and Y0 <= c[1] <= Y1


def clear_footprint(w):
    removed = 0
    for a in list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.CoverActor)):
        if any(in_footprint(c) for c in cells(a)):
            ACT.destroy_actor(a)
            removed += 1
    report.append("removed %d cover pieces inside the farmhouse footprint" % removed)


def runs():
    """Straight wall runs: (axis, line, [along-coordinates]), split where the plan has no wall."""
    walls = {c for c, ch in PLAN.items() if ch in WALL_CHARS}
    along_x = {c for c in walls if (c[0] - 1, c[1]) in walls or (c[0] + 1, c[1]) in walls}
    along_y = walls - along_x
    out = []
    for axis, group in (("x", along_x), ("y", along_y)):
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


def end_extension(axis, line, v, step, walls, along_x):
    """Extension (cm) for the run end at v; step is -1 for the start end, +1 for the far end."""
    if axis == "x":
        x, y = cell_of(axis, line, v)
        return -35.0 if ((x, y - 1) in walls or (x, y + 1) in walls) else 0.0
    nxt = cell_of(axis, line, v + step)
    if nxt in along_x:
        return 35.0
    if nxt in walls:
        raise RuntimeError("Y run meets another Y run at %s" % (nxt,))
    return 0.0


def split(chars):
    """Pieces of one run: (kind, first index, last index), kind wall / window / breach."""
    n = len(chars)

    def starts_window(k):
        return chars[k] == "#" and k + 2 < n and chars[k + 1] == "w" and chars[k + 2] == "#"

    out, i = [], 0
    while i < n:
        if chars[i] == "B":
            out.append(("breach", i, i))
            i += 1
        elif starts_window(i):
            j = i + 2
            while j + 2 < n and chars[j + 1] == "w" and chars[j + 2] == "#":
                j += 2
            out.append(("window", i, j))
            i = j + 1
        elif chars[i] == "#":
            j = i
            while j + 1 < n and chars[j + 1] == "#" and not starts_window(j + 1):
                j += 1
            out.append(("wall", i, j))
            i = j + 1
        else:
            raise RuntimeError("window without wall either side in run %r" % chars)
    return out


CLASSES = {"wall": unreal.WallCover, "window": unreal.WindowWallCover, "breach": unreal.BreachableWall}


def place_piece(kind, axis, line, first, last, ext_lo, ext_hi):
    """One piece over cells first..last of a run, its low-coordinate end extended by ext_lo, the high one by ext_hi."""
    want = {cell_of(axis, line, v) for v in range(first, last + 1)}
    piece = ACT.spawn_actor_from_class(CLASSES[kind], world(*cell_of(axis, line, first)), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    piece.set_editor_property("Length", last - first + 1)
    if kind == "window":
        piece.set_editor_property("WindowSpacing", 2)
    piece.set_folder_path(FOLDER)
    piece.set_actor_label("B_%s_%s%d_%d-%d" % (kind, axis, line, first, last))
    mid = (first + last) // 2
    yaw = 0.0 if axis == "x" else 90.0
    for d in (0, 1, -1):
        for turn in (yaw, yaw + 180.0):
            cx, cy = cell_of(axis, line, mid + d)
            piece.set_actor_location_and_rotation(world(cx, cy), unreal.Rotator(roll=0.0, pitch=0.0, yaw=turn), False, False)
            if cells(piece) == want:
                # Local +X points up the run unless the piece was turned round.
                lo_is_start = turn == yaw
                piece.set_editor_property("StartExtension", ext_lo if lo_is_start else ext_hi)
                piece.set_editor_property("EndExtension", ext_hi if lo_is_start else ext_lo)
                return 0
    report.append("MISMATCH %s: %s vs %s" % (piece.get_actor_label(), sorted(cells(piece)), sorted(want)))
    return 1


def build_walls():
    wall_runs, walls, along_x = runs()
    bad = pieces = 0
    for axis, line, vs in wall_runs:
        chars = "".join(PLAN[cell_of(axis, line, v)] for v in vs)
        lo_ext = end_extension(axis, line, vs[0], -1, walls, along_x)
        hi_ext = end_extension(axis, line, vs[-1], 1, walls, along_x)
        parts = split(chars)
        for k, (kind, i, j) in enumerate(parts):
            bad += place_piece(kind, axis, line, vs[i], vs[j], lo_ext if k == 0 else 0.0, hi_ext if k == len(parts) - 1 else 0.0)
            pieces += 1
        report.append("run %s=%d %d-%d %r ends %+.0f/%+.0f -> %s" % (axis, line, vs[0], vs[-1], chars, lo_ext, hi_ext,
                                                                  [(kind, vs[i], vs[j]) for kind, i, j in parts]))
    report.append("walls: %d pieces, %d mismatched" % (pieces, bad))
    return bad


def build_props():
    bad = placed = 0
    for (x, y), ch in sorted(PLAN.items()):
        if ch not in PROPS:
            continue
        prop = ACT.spawn_actor_from_class(PROPS[ch], world(x, y), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
        prop.set_folder_path(FOLDER)
        prop.set_actor_label("B_prop_%d_%d" % (x, y))
        if cells(prop) != {(x, y)}:
            report.append("PROP MISMATCH %s at %s: %s" % (prop.get_class().get_name(), (x, y), sorted(cells(prop))))
            bad += 1
        placed += 1
    report.append("props: placed %d" % placed)
    return bad


def floors(w):
    """Planks over the whole farmhouse, flagstones in the keep; drops the old road spur into the yard."""
    by_label = {}
    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.StaticMeshActor):
        by_label[a.get_actor_label()] = a
        if str(a.get_folder_path()) == "Farmstead/Roads":
            origin, extent = a.get_actor_bounds(False)
            lo = (origin.x - extent.x + 3150.0, origin.y - extent.y + 3150.0)
            hi = (origin.x + extent.x + 3150.0, origin.y + extent.y + 3150.0)
            if lo[0] >= X0 * 100 - 1 and hi[0] <= (X1 + 1) * 100 + 1 and lo[1] >= Y0 * 100 - 1 and hi[1] <= (Y1 + 1) * 100 + 1:
                report.append("removed road %s inside the farmhouse" % a.get_actor_label())
                ACT.destroy_actor(a)
    cx, cy = (X0 + X1) / 2.0, (Y0 + Y1) / 2.0
    planks = by_label["Floor_B3"]
    planks.set_actor_location(world(cx, cy, 0.3), False, False)
    planks.set_actor_scale3d(unreal.Vector(X1 - X0 + 1, Y1 - Y0 + 1, 1.0))
    keep = by_label.get("Floor_B_Keep")
    if keep is None:
        keep = ACT.duplicate_actor(by_label["Floor_B5"])  # flagstone
        keep.set_actor_label("Floor_B_Keep")
        keep.set_folder_path("Farmstead/Floors")
    keep.set_actor_location(world(32.5, 31.5, 0.6), False, False)
    keep.set_actor_scale3d(unreal.Vector(6.0, 6.0, 1.0))
    report.append("floors: planks %dx%d, keep flagstones 6x6" % (X1 - X0 + 1, Y1 - Y0 + 1))


# Weakened walls beside the C room's corners: stretch them to the corner post at (57, y), like the plain wall pieces.
C_ROOM_BREACHABLES = [(56, 28), (56, 35)]


def fix_c_room(w):
    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.BreachableWall):
        if cells(a) & set(C_ROOM_BREACHABLES):
            yaw = a.get_actor_rotation().yaw
            east_is_end = round(math.cos(math.radians(yaw))) > 0
            a.set_editor_property("EndExtension" if east_is_end else "StartExtension", 50.0)
            report.append("C room: stretched %s at %s to its corner" % (a.get_actor_label(), sorted(cells(a))))


def check_symmetry(w):
    shapes = set()
    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.CoverActor):
        shapes.add((a.get_class().get_name(), frozenset(cells(a))))
    odd = [s for s in shapes if (s[0], frozenset((x, 63 - y) for x, y in s[1])) not in shapes]
    for cls, cs in odd:
        report.append("ASYMMETRIC %s %s" % (cls, sorted(cs)))
    report.append("symmetry: %d pieces without a mirror image" % len(odd))
    return len(odd)


def main():
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not w.get_name().endswith("Farmstead"):
        report.append("ABORT: open %s first (got %s)" % (LEVEL, w.get_name()))
        return
    clear_footprint(w)
    bad = build_walls()
    bad += build_props()
    floors(w)
    fix_c_room(w)
    bad += check_symmetry(w)
    if bad:
        report.append("NOT SAVED: fix the problems first")
        return
    # Git LFS checks out lockable .umap files read-only: clear the flag first (attrib -r), or this fails.
    if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
        report.append("saved %s" % LEVEL)
    else:
        report.append("SAVE FAILED for %s (read-only file?)" % LEVEL)


try:
    main()
except Exception as e:  # Report instead of leaving the editor open with no output.
    report.append("ERROR %r" % e)
open(REPORT, "w").write("\n".join(report) + "\n")
unreal.SystemLibrary.quit_editor()
