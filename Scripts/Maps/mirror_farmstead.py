"""Makes Farmstead fair: mirrors the south half of the level onto the north half (GDD §10.5, v0.55).

AI test matches showed the old layout favoured the south start side (31-2 over 33 decided 4v4 matches, whichever faction
started there). Mirroring the south half was the fairest of the layouts tried (Docs/Balancing/2026-11-25_map_bias).

Mirror line: between cell rows 31 and 32 (world Y = 0 for the centred 64x64 grid), so cell (x, y) maps to
(x, 63 - y). Both deployment zones (rows 1-8 / 55-62), the three VPs (rows 29-34) and the level's squads are
already mirror images and are left alone.

- Cover actors wholly in the north half (rows 32+) are deleted. Every other cover actor gets a mirrored copy
  (location Y -> -Y, yaw -> -yaw, nudged one cell if an even-length piece lands off by one). Its occupied cells
  are checked against the mirror of the original's.
- Floor planes (roads, ponds, building floors): north ones are deleted, south ones mirrored, and a plane that
  crosses the mirror line is resized to its south part plus that part's mirror image.
- Weakened walls: BREACHABLE_CELLS, in mirrored pairs, replace the full-wall cells there (place_breachables).

Run in the full editor with Farmstead open (actor duplication crashes the headless commandlet):
    UnrealEditor.exe Unreal.uproject /Game/Maps/Farmstead -ExecCmds="py C:/.../Scripts/Maps/mirror_farmstead.py"
It saves the level and quits the editor. Writes a report to Saved/MapAudit/mirror_farmstead.txt.
Running it twice is harmless: the second pass finds a symmetric level and replaces each north piece with an
identical copy.
"""
import unreal

LEVEL = "/Game/Maps/Farmstead"
REPORT = unreal.Paths.project_saved_dir() + "MapAudit/mirror_farmstead.txt"
ACT = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

report = []


def cells(actor):
    return {(c.x, c.y) for c in actor.get_occupied_cells()}


def mirror_cover(world):
    removed = mirrored = bad = 0
    for a in list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CoverActor)):
        occupied = cells(a)
        if occupied and all(y >= 32 for (_, y) in occupied):
            ACT.destroy_actor(a)
            removed += 1
    remaining = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CoverActor))
    existing = {(a.get_class().get_name(), frozenset(cells(a))) for a in remaining}
    for a in remaining:
        occupied = cells(a)
        if not occupied:
            continue
        want = {(x, 63 - y) for (x, y) in occupied}
        if want == occupied or (a.get_class().get_name(), frozenset(want)) in existing:
            continue  # Its own mirror image, or the mirror copy is already there (second run).
        dup = ACT.duplicate_actor(a)
        loc = a.get_actor_location()
        yaw = a.get_actor_rotation().yaw
        ok = False
        for dx, dy in ((0, 0), (0, 100), (0, -100), (100, 0), (-100, 0)):
            dup.set_actor_location_and_rotation(unreal.Vector(loc.x + dx, -loc.y + dy, loc.z),
                                                unreal.Rotator(roll=0.0, pitch=0.0, yaw=-yaw), False, False)
            if cells(dup) == want:
                ok = True
                break
        if not ok:
            bad += 1
            report.append("MISMATCH %s: %s vs %s" % (a.get_actor_label(), sorted(cells(dup)), sorted(want)))
        mirrored += 1
    report.append("cover: removed %d north pieces, mirrored %d, mismatched %d" % (removed, mirrored, bad))
    return bad


# A merged line that should end at corner pieces: (axis, line coordinate) -> (first, last) cell along the line.
# The B yard's west wall: windows only between its corners at (29, 30) and (29, 33).
TRIM = {("x", 29): (31, 32)}


def place_run(template, axis, line, first, last):
    """A copy of template covering cells first..last along the line (x = line for axis "x", else y = line)."""
    want = {(line, v) for v in range(first, last + 1)} if axis == "x" else {(v, line) for v in range(first, last + 1)}
    piece = ACT.duplicate_actor(template)
    piece.set_editor_property("Length", last - first + 1)
    mid = (first + last) // 2
    cx, cy = (line, mid) if axis == "x" else (mid, line)
    yaw = 90.0 if axis == "x" else 0.0
    z = template.get_actor_location().z
    for dx, dy in ((0, 0), (0, 1), (0, -1), (1, 0), (-1, 0)):
        for turn in (yaw, yaw + 180.0):
            loc = unreal.Vector((cx + dx) * 100.0 - 3150.0, (cy + dy) * 100.0 - 3150.0, z)
            piece.set_actor_location_and_rotation(loc, unreal.Rotator(roll=0.0, pitch=0.0, yaw=turn), False, False)
            if cells(piece) == want:
                return True
    report.append("MERGE MISMATCH %s: %s vs %s" % (template.get_actor_label(), sorted(cells(piece)), sorted(want)))
    return False


def merge_overlaps(world):
    """Pieces that crossed the mirror line overlap their mirror copies: replace each overlapping group of
    straight pieces of one class on one line by single pieces over the cells they cover together."""
    covers = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CoverActor))
    owners = {}
    for a in covers:
        for c in cells(a):
            owners.setdefault(c, []).append(a)
    overlapping = {a for group in owners.values() if len(group) > 1 for a in group}
    groups = {}
    for a in overlapping:
        occ = cells(a)
        xs, ys = {x for x, _ in occ}, {y for _, y in occ}
        if len(xs) == 1 and len(ys) > 1:
            key = (a.get_class().get_name(), "x", next(iter(xs)))
        elif len(ys) == 1 and len(xs) > 1:
            key = (a.get_class().get_name(), "y", next(iter(ys)))
        else:
            continue  # Single-cell pieces (corners) stay; the line pieces round them are merged.
        groups.setdefault(key, []).append(a)
    merged = bad = 0
    for (cls, axis, line), pieces in sorted(groups.items(), key=lambda kv: kv[0]):
        if len(pieces) < 2:
            continue
        along = sorted({(y if axis == "x" else x) for a in pieces for (x, y) in cells(a)})
        others = {c for a in covers if a not in pieces for c in cells(a)}
        along = [v for v in along if ((line, v) if axis == "x" else (v, line)) not in others]
        if (axis, line) in TRIM:
            lo, hi = TRIM[(axis, line)]
            along = [v for v in along if lo <= v <= hi]
        runs, run = [], []
        for v in along:
            if run and v != run[-1] + 1:
                runs.append(run)
                run = []
            run.append(v)
        if run:
            runs.append(run)
        template = pieces[0]
        for r in runs:
            if not place_run(template, axis, line, r[0], r[-1]):
                bad += 1
        for a in pieces:
            ACT.destroy_actor(a)
        merged += 1
        report.append("merged %d %s on %s=%d into runs %s" % (len(pieces), cls, axis, line, [(r[0], r[-1]) for r in runs]))
    report.append("overlaps: merged %d groups, mismatched %d" % (merged, bad))
    return bad


def mirror_floors(world):
    removed = mirrored = resized = 0
    for a in list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor)):
        if not str(a.get_folder_path()).startswith("Farmstead/"):
            continue
        origin, extent = a.get_actor_bounds(False)
        ymin, ymax = origin.y - extent.y, origin.y + extent.y
        if ymin >= -1.0:
            ACT.destroy_actor(a)
            removed += 1
        elif ymax <= 1.0:
            dup = ACT.duplicate_actor(a)
            loc = a.get_actor_location()
            dup.set_actor_location(unreal.Vector(loc.x, -loc.y, loc.z), False, False)
            mirrored += 1
        else:
            # Crosses the line: keep its south part and that part's mirror, i.e. span ymin .. -ymin around Y = 0.
            scale = a.get_actor_scale3d()
            loc = a.get_actor_location()
            new_half = -ymin
            a.set_actor_scale3d(unreal.Vector(scale.x, scale.y * new_half / extent.y, scale.z))
            a.set_actor_location(unreal.Vector(loc.x, 0.0, loc.z), False, False)
            resized += 1
    report.append("floors: removed %d north planes, mirrored %d, resized %d across the line" % (removed, mirrored, resized))


# Weakened walls (GDD §9.4, §10.5), in mirrored pairs: one full-wall cell on each side of the B yard and of the
# C room, next to the doorway, so a breach opens a second way in from either side. Each wall runs along X.
BREACHABLE_CELLS = [(36, 30), (36, 33), (56, 28), (56, 35)]


def place_breachables(world):
    placed = 0
    covers = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CoverActor))
    for cell in BREACHABLE_CELLS:
        here = [a for a in covers if cell in cells(a)]
        if any(isinstance(a, unreal.BreachableWall) for a in here):
            continue
        z = here[0].get_actor_location().z if here else 0.0
        for a in here:
            if len(cells(a)) != 1:
                report.append("BREACHABLE %s: cell is part of a longer piece %s" % (cell, a.get_actor_label()))
                return 1
            ACT.destroy_actor(a)
        wall = ACT.spawn_actor_from_class(unreal.BreachableWall, unreal.Vector(cell[0] * 100.0 - 3150.0, cell[1] * 100.0 - 3150.0, z),
                                          unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
        wall.set_editor_property("Length", 1)
        wall.set_folder_path("Farmstead/Cover")
        if cells(wall) != {cell}:
            report.append("BREACHABLE MISMATCH %s: %s" % (cell, sorted(cells(wall))))
            return 1
        placed += 1
    report.append("breachable walls: placed %d" % placed)
    return 0


def main():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not world.get_name().endswith("Farmstead"):
        report.append("ABORT: open %s first (got %s)" % (LEVEL, world.get_name()))
        return
    bad = mirror_cover(world)
    bad += merge_overlaps(world)
    bad += place_breachables(world)
    mirror_floors(world)
    if bad:
        report.append("NOT SAVED: fix the mismatches first")
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
