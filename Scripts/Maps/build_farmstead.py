"""
Builds /Game/Maps/Farmstead: a placeholder 64x64 farm map (2v2 size, GDD §10.1).

Run from the Unreal Editor console (Cmd):  py Scripts/Maps/build_farmstead.py
Call validate() alone to check the layout without touching the level.

Re-running is safe: it deletes everything it placed before (cover actors and actors tagged
"FarmsteadGen") and rebuilds from the layout below.

Layout is in grid cells: (row, col), both 0..63. Row 0 is the north edge (world +X),
col 0 is the west edge (world -Y). One cell = 100 uu.

Gameplay pieces use the project's cover classes (hay bales, low walls, full walls, window walls,
trees). Roads and ponds are flat coloured planes with no collision: purely visual (no water rule yet).
Deployment Zones and Victory Points are ADeploymentZone / AVictoryPoint actors placed per GDD §10.5;
this script leaves them alone and starts each squad inside its own DZ.
"""
import unreal

MAP_PATH = "/Game/Maps/Farmstead"
TEMPLATE_MAP = "/Game/Maps/Main"
MAT_DIR = "/Game/Materials/Placeholder"
GRID = 64
CELL = 100.0
HALF = (GRID - 1) / 2.0  # 31.5
TAG = "FarmsteadGen"
MAX_PIECE = 15

# ---------------------------------------------------------------------------------------------
# Layout
# ---------------------------------------------------------------------------------------------

# Straight runs: (class, (r0, c0), (r1, c1)) - must be horizontal or vertical. Inclusive.
LOW_WALLS = [
    # Perimeter (gaps: north cols 8-10, west rows 40-42, south cols 18-20)
    ((0, 0), (0, 7)), ((0, 11), (0, 63)),
    ((63, 0), (63, 17)), ((63, 21), (63, 63)),
    ((1, 0), (39, 0)), ((43, 0), (62, 0)),
    ((1, 63), (62, 63)),
    # Top-left enclosure (gap cols 4-5 and at the spine)
    ((18, 1), (18, 3)), ((18, 6), (18, 20)),
    # Orchard wall (gap cols 40-42; road gap at 18-20 is west of it)
    ((47, 21), (47, 39)), ((47, 43), (47, 62)),
    # Bottom-left field wall
    ((47, 1), (47, 15)),
    # Pond field west wall (gap rows 37-38)
    ((29, 45), (36, 45)), ((39, 45), (46, 45)),
]

HEDGES = [  # low walls too, just shorter runs
    ((30, 3), (30, 7)), ((36, 3), (36, 7)),
    ((20, 26), (20, 28)), ((44, 26), (44, 31)),
    ((44, 36), (44, 40)), ((24, 48), (24, 52)),
    ((10, 44), (14, 44)), ((52, 4), (52, 9)),
]

# Buildings: outer rectangle (r0, c0, r1, c1) plus door gaps (side, from, to) on that side.
# side: "n"/"s" gaps are column ranges, "w"/"e" gaps are row ranges.
# kind: "window" (window walls: solid + windows) or "frame" (ruin: open frame, window every other cell)
BUILDINGS = [
    # B1 big farmhouse, top centre
    dict(rect=(6, 28, 14, 40), kind="window", doors=[("s", 33, 35), ("w", 9, 10)]),
    # B2 ruined frame building, top right
    dict(rect=(3, 46, 8, 52), kind="frame", doors=[("s", 48, 50)]),
    # B3 central farmhouse (main block)
    dict(rect=(26, 30, 34, 44), kind="window", doors=[("w", 29, 31), ("n", 32, 34), ("n", 40, 41)]),
    # B4 small shed, left
    dict(rect=(22, 10, 27, 17), kind="window", doors=[("e", 24, 25)]),
    # B5 ruined barn frame, left-lower
    dict(rect=(33, 12, 38, 19), kind="frame", doors=[("n", 15, 16), ("e", 35, 36)]),
]

# Interior full walls (internal partitions, the L-wing of the central farmhouse)
FULL_WALLS = [
    ((7, 35), (9, 35)),           # B1 partition, doorway at rows 10-11 into the east room
    ((12, 35), (13, 35)),
    ((21, 38), (22, 38)),         # B3 L-wing west wall, doorway at rows 23-24 onto the courtyard
    ((25, 38), (25, 38)),
    ((21, 39), (21, 44)),         # B3 L-wing north wall
    ((22, 44), (25, 44)),         # B3 L-wing east wall
    ((30, 37), (33, 37)),         # B3 interior partition
]

# Weakened full walls (GDD §9.4): these wall cells are built as ABreachableWall instead of a normal wall.
# (row, col) here; GDD cell (X, Y) = (63 - row, col). Listed in GDD §10.5.
BREACHABLE_WALLS = [
    ((7, 35), (9, 35)),           # B1 partition = GDD 54-56, 35: second way into VP C's room
    ((30, 37), (32, 37)),         # B3 interior partition = GDD 31-33, 37: flank into VP B
]

TREES = (
    # Orchard (south)
    [(r, c) for r in (50, 54, 58) for c in (24, 29, 34, 39, 44, 49, 54, 59)]
    # Bottom-left field
    + [(51, 12), (56, 6), (58, 13), (55, 3)]
    # East edge
    + [(10, 60), (16, 60), (22, 60), (28, 60), (8, 56), (26, 56)]
    # Poplar row, top left
    + [(14, c) for c in (12, 14, 16, 18, 20)]
    # West side
    + [(26, 4), (32, 3), (45, 8), (4, 4), (6, 16)]
    # North fields
    + [(3, 24), (4, 42), (12, 54)]
)

HAY = [  # ((r, c), length, along) along: "row" = runs east-west, "col" = runs north-south
    # VP3 field
    ((16, 49), 2, "row"), ((19, 53), 1, "row"), ((17, 55), 1, "row"), ((20, 48), 1, "row"),
    # Top-left enclosure
    ((3, 14), 1, "row"), ((5, 19), 1, "row"), ((8, 5), 1, "row"), ((15, 4), 2, "row"),
    # Top fields
    ((2, 30), 1, "row"), ((3, 36), 1, "row"), ((2, 44), 1, "row"), ((10, 50), 1, "row"),
    # Left field
    ((24, 21), 1, "row"), ((29, 9), 2, "col"), ((44, 4), 1, "row"), ((44, 14), 2, "row"),
    ((50, 8), 1, "row"), ((60, 9), 2, "row"),
    # Around the central farmhouse
    ((36, 30), 2, "row"), ((37, 26), 1, "row"), ((22, 27), 1, "row"), ((16, 42), 1, "row"),
    # Pond field (crates / bales)
    ((30, 52), 1, "row"), ((33, 48), 1, "row"), ((38, 55), 1, "row"), ((42, 52), 1, "row"),
    ((45, 60), 1, "row"), ((35, 61), 1, "row"), ((41, 46), 1, "row"),
    # Orchard
    ((49, 37), 1, "row"), ((56, 27), 1, "row"), ((60, 47), 2, "row"),
]

# Flat decoration: (r0, c0, r1, c1) inclusive
ROADS = [
    (40, 0, 42, 24),     # west entrance -> junction
    (10, 22, 42, 24),    # north-south spine
    (10, 8, 12, 21),     # spine -> north-west
    (0, 8, 9, 10),       # north gate
    (42, 18, 63, 20),    # junction -> south gate
    (24, 25, 26, 29),    # spine -> central farmhouse
    (15, 25, 17, 33),    # spine -> big farmhouse
]
PONDS = [
    (34, 50, 37, 54), (40, 47, 42, 50), (38, 57, 41, 60),
    (31, 57, 33, 61), (43, 53, 45, 57), (27, 47, 29, 49),
]
# Deployment Zones and Victory Points are their own actors (ADeploymentZone / AVictoryPoint, folder
# Farmstead/Zones), placed per GDD §10.5 and left alone by this script. The DZs, in this script's
# (row, col) cells, are below: GDD (X, Y) = (col, 63 - row) in world-grid terms, i.e. the GDD's
# "south" edge is this layout's west edge (col 0) and "north" is the east edge (col 63).
ALLIED_DZ = (22, 1, 29, 8)     # (r0, c0, r1, c1) inclusive = GDD cells X 34-41, Y 1-8
AXIS_DZ = (22, 55, 29, 62)     # = GDD cells X 34-41, Y 55-62

# Each squad starts in a line across the middle of its own DZ, facing the other DZ.
ALLIED_SOLDIERS = [(24, 5), (25, 5), (26, 5), (27, 5)]
AXIS_SOLDIERS = [(24, 58), (25, 58), (26, 58), (27, 58)]
PLAYER_START = (23, 4)

# ---------------------------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------------------------

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAL = unreal.EditorAssetLibrary


def world_xy(r, c):
    return unreal.Vector((HALF - r) * CELL, (c - HALF) * CELL, 0.0)


def spawn(cls, r, c, yaw=0.0, label=None):
    a = eas.spawn_actor_from_class(cls, world_xy(r, c), unreal.Rotator(0.0, 0.0, yaw))
    a.modify()
    a.tags = [TAG]
    if label:
        a.set_actor_label(label)
    return a


def chunks(n):
    """Split a run of n cells into odd-length pieces of at most MAX_PIECE (odd pieces centre on a cell)."""
    out = []
    while n > 0:
        k = min(n, MAX_PIECE)
        if k % 2 == 0:
            k -= 1
        out.append(k)
        n -= k
    return out


def _line(a, b):
    (r0, c0), (r1, c1) = a, b
    if r0 != r1 and c0 != c1:
        raise ValueError(f"run {a}->{b} is not straight")
    if r0 == r1:
        return [(r0, c) for c in range(min(c0, c1), max(c0, c1) + 1)]
    return [(r, c0) for r in range(min(r0, r1), max(r0, r1) + 1)]


# Wall slab thickness per cover class, in cm (matches CoverActor.cpp). Used to close corners exactly.
SLAB_THICKNESS = {"LowWallCover": 40.0, "WallCover": 30.0, "WindowWallCover": 30.0, "BreachableWall": 30.0}


def wall_runs():
    """
    Every straight wall run, after cutting out door gaps:
    dict(cls, cells ordered low->high index, horizontal, folder, props).
    """
    runs = []

    def add(cls, a, b, folder, gaps=(), props=None):
        horizontal = a[0] == b[0]
        seg = []
        for cell in _line(a, b) + [None]:
            index = None if cell is None else (cell[1] if horizontal else cell[0])
            if cell is None or any(g0 <= index <= g1 for g0, g1 in gaps):
                if seg:
                    runs.append(dict(cls=cls, cells=seg, horizontal=horizontal, folder=folder, props=props or {}))
                seg = []
            else:
                seg.append(cell)

    for a, b in LOW_WALLS:
        add("LowWallCover", a, b, "Farmstead/Walls")
    for a, b in HEDGES:
        add("LowWallCover", a, b, "Farmstead/Hedges")
    for a, b in FULL_WALLS:
        add("WallCover", a, b, "Farmstead/Buildings/Interior")
    for i, spec in enumerate(BUILDINGS):
        r0, c0, r1, c1 = spec["rect"]
        folder = f"Farmstead/Buildings/B{i + 1}"
        props = {"WindowSpacing": 2 if spec["kind"] == "frame" else 3}
        gaps = {s: [(g0, g1) for side, g0, g1 in spec["doors"] if side == s] for s in "nswe"}
        add("WindowWallCover", (r0, c0), (r0, c1), folder, gaps["n"], props)
        add("WindowWallCover", (r1, c0), (r1, c1), folder, gaps["s"], props)
        add("WindowWallCover", (r0 + 1, c0), (r1 - 1, c0), folder, gaps["w"], props)
        add("WindowWallCover", (r0 + 1, c1), (r1 - 1, c1), folder, gaps["e"], props)
    return breachable_segments(solid_junctions(runs))


def breachable_segments(runs):
    """Split full-wall runs so the cells in BREACHABLE_WALLS become their own BreachableWall pieces (GDD §9.4)."""
    weak = {cell for a, b in BREACHABLE_WALLS for cell in _line(a, b)}
    out = []
    for wr in runs:
        if wr["cls"] != "WallCover" or not weak.intersection(wr["cells"]):
            out.append(wr)
            continue
        seg, seg_weak = [], None
        for cell in wr["cells"] + [None]:
            is_weak = None if cell is None else cell in weak
            if seg and (cell is None or is_weak != seg_weak):
                out.append(dict(wr, cls="BreachableWall" if seg_weak else "WallCover", cells=seg))
                seg = []
            if cell is not None:
                seg.append(cell)
                seg_weak = is_weak
    return out


def solid_junctions(runs):
    """
    A window right where two walls meet (a corner, or where an inside wall joins an outside one) leaves
    the walls looking unjoined. Any window-wall cell that touches a perpendicular wall becomes a solid
    one-piece WallCover (the same cover as a window wall's solid cells); windows stay everywhere else.
    """
    orientation = {}
    for wr in runs:
        for cell in wr["cells"]:
            orientation[cell] = wr["horizontal"]

    def at_junction(cell, horizontal):
        return any(orientation.get((cell[0] + dr, cell[1] + dc), horizontal) != horizontal
                   for dr, dc in ((0, 1), (0, -1), (1, 0), (-1, 0)))

    out = []
    for wr in runs:
        if wr["cls"] != "WindowWallCover":
            out.append(wr)
            continue
        seg, seg_solid = [], None
        for cell in wr["cells"] + [None]:
            solid = None if cell is None else at_junction(cell, wr["horizontal"])
            if seg and (cell is None or solid != seg_solid):
                out.append(dict(wr, cls="WallCover" if seg_solid else "WindowWallCover", cells=seg,
                                props={} if seg_solid else wr["props"]))
                seg = []
            if cell is not None:
                seg.append(cell)
                seg_solid = solid
    return out


def corner_extensions(runs):
    """
    How far to stretch (+) or trim (-) each run's two end slabs, in cm, so walls meet cleanly.
    A slab runs through the middle of its cells, so a run that ends beside a perpendicular wall stops
    half a cell short of it. That run stretches half a cell into the other wall's cell; the wall whose
    end cell is the corner is trimmed back to the other wall's outer face.
    Visual and physical only: the grid cells each piece claims are unchanged.
    """
    slabs = {}
    for wr in runs:
        for cell in wr["cells"]:
            slabs[cell] = (wr["horizontal"], SLAB_THICKNESS[wr["cls"]])

    def end_ext(cell, outward, horizontal):
        beyond = (cell[0] + outward[0], cell[1] + outward[1])
        if beyond in slabs:
            # Perpendicular wall ahead: stretch into it. Same direction: the wall carries on in another
            # piece (e.g. solid cell at a junction), so leave the end as it is.
            return CELL / 2 if slabs[beyond][0] != horizontal else 0.0
        side = (outward[1], outward[0])
        perpendicular = [slabs[n] for n in ((cell[0] + side[0], cell[1] + side[1]), (cell[0] - side[0], cell[1] - side[1]))
                         if n in slabs and slabs[n][0] != horizontal]
        if perpendicular:
            return -(CELL / 2 - max(t for _, t in perpendicular) / 2)
        return 0.0

    for wr in runs:
        h = wr["horizontal"]
        wr["ext_lo"] = end_ext(wr["cells"][0], (0, -1) if h else (-1, 0), h)
        wr["ext_hi"] = end_ext(wr["cells"][-1], (0, 1) if h else (1, 0), h)
    return runs


def run(cls, cells, horizontal, folder, props=None, ext_lo=0.0, ext_hi=0.0):
    """Spawn a straight run (cells ordered low->high index) as odd-length pieces of cls (a class name)."""
    pieces = chunks(len(cells))
    start = 0
    for n, k in enumerate(pieces):
        r, c = cells[start + (k - 1) // 2]
        # Actor local X runs along the piece: yaw 90 => along world Y (a row; local -X = low column),
        # yaw 0 => along world X (a column; local -X = south = high row).
        actor = spawn(getattr(unreal, cls), r, c, 90.0 if horizontal else 0.0)
        # set_editor_property re-runs OnConstruction, so the piece rebuilds with each new value.
        # (AActor::RerunConstructionScripts is not exposed to Python.)
        actor.set_editor_property("Length", k)
        for key, val in (props or {}).items():
            actor.set_editor_property(key, val)
        lo_ext = ext_lo if n == 0 else 0.0
        hi_ext = ext_hi if n == len(pieces) - 1 else 0.0
        start_ext, end_ext = (lo_ext, hi_ext) if horizontal else (hi_ext, lo_ext)
        if start_ext:
            actor.set_editor_property("StartExtension", start_ext)
        if end_ext:
            actor.set_editor_property("EndExtension", end_ext)
        actor.set_folder_path(folder)
        start += k


def hay_cells(r, c, length, along):
    return [(r, c + i) if along == "row" else (r + i, c) for i in range(length)]


def load_material(name):
    mi = unreal.load_asset(f"{MAT_DIR}/{name}")
    if not mi:
        raise RuntimeError(f"Missing {MAT_DIR}/{name} (instance of /Engine/BasicShapes/BasicShapeMaterial with a Color override)")
    return mi


PLANE = None


def flat(rect, mat, z, folder, label=None):
    r0, c0, r1, c1 = rect
    loc = unreal.Vector((HALF - (r0 + r1) / 2.0) * CELL, ((c0 + c1) / 2.0 - HALF) * CELL, z)
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, loc, unreal.Rotator(0, 0, 0))
    a.modify()
    smc = a.static_mesh_component
    a.tags = [TAG]
    smc.modify()
    smc.set_static_mesh(PLANE)
    smc.set_material(0, mat)
    smc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    smc.set_editor_property("cast_shadow", False)
    a.set_actor_scale3d(unreal.Vector(r1 - r0 + 1, c1 - c0 + 1, 1))
    a.set_folder_path(folder)
    if label:
        a.set_actor_label(label)
    return a


# ---------------------------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------------------------

def validate():
    """
    Warn about cover pieces sharing a cell, sitting off the map, on a road or pond, or on a spawn, and about
    open ground soldiers can't walk to from the player spawn (8-way moves, no corner cutting, GDD §10.2).
    """
    occ, problems = {}, []

    def add(cells, what):
        for cell in cells:
            if not (0 <= cell[0] < GRID and 0 <= cell[1] < GRID):
                problems.append(f"off map: {what} {cell}")
            if cell in occ:
                problems.append(f"overlap: {cell} {occ[cell]} / {what}")
            occ[cell] = what

    for wr in wall_runs():
        add(wr["cells"], f"{wr['folder']} {wr['cls']} {wr['cells'][0]}")
    for t in TREES:
        add([t], f"tree {t}")
    for (r, c), length, along in HAY:
        add(hay_cells(r, c, length, along), f"hay {(r, c)}")

    def cells_of(rects):
        return {(r, c) for r0, c0, r1, c1 in rects for r in range(r0, r1 + 1) for c in range(c0, c1 + 1)}

    for cell in cells_of(ROADS) | cells_of(PONDS):
        if cell in occ and "Buildings" not in occ[cell]:
            problems.append(f"on road/pond: {cell} {occ[cell]}")
    for cell in ALLIED_SOLDIERS + AXIS_SOLDIERS + [PLAYER_START]:
        if cell in occ:
            problems.append(f"spawn blocked: {cell} {occ[cell]}")
    for cells, dz, name in ((ALLIED_SOLDIERS + [PLAYER_START], ALLIED_DZ, "Allied"), (AXIS_SOLDIERS, AXIS_DZ, "Axis")):
        for cell in cells:
            if cell not in cells_of([dz]):
                problems.append(f"{name} spawn {cell} is outside the {name} DZ")

    # Reachability from the Allied spawn (also covers the Axis spawn, which must be reachable too)
    def free(cell):
        return 0 <= cell[0] < GRID and 0 <= cell[1] < GRID and cell not in occ

    seen = {ALLIED_SOLDIERS[0]}
    stack = [ALLIED_SOLDIERS[0]]
    while stack:
        r, c = stack.pop()
        for dr in (-1, 0, 1):
            for dc in (-1, 0, 1):
                n = (r + dr, c + dc)
                if n in seen or not free(n):
                    continue
                if dr and dc and not (free((r + dr, c)) and free((r, c + dc))):
                    continue
                seen.add(n)
                stack.append(n)
    unreachable = {(r, c) for r in range(GRID) for c in range(GRID) if free((r, c)) and (r, c) not in seen}
    while unreachable:
        group, todo = set(), [unreachable.pop()]
        while todo:
            r, c = todo.pop()
            group.add((r, c))
            for n in ((r + 1, c), (r - 1, c), (r, c + 1), (r, c - 1)):
                if n in unreachable:
                    unreachable.discard(n)
                    todo.append(n)
        rows = [g[0] for g in group]
        cols = [g[1] for g in group]
        problems.append(f"unreachable: {len(group)} cells, rows {min(rows)}-{max(rows)} cols {min(cols)}-{max(cols)}")
    for p in problems:
        unreal.log_warning("Farmstead layout: " + p)
    return problems


def build():
    global PLANE
    problems = validate()
    print("Farmstead layout problems:", len(problems))
    for p in problems:
        print("  -", p)
    # Duplicating the .umap and then loading it trips a UE GC fatal error ("Old world not cleaned up"),
    # so a first build opens the template and saves it under the new name at the end.
    first_build = not EAL.does_asset_exist(MAP_PATH)
    les.load_level(TEMPLATE_MAP if first_build else MAP_PATH)

    # Clear previous generation, the template's test cover and its extra test enemies
    for a in eas.get_all_level_actors():
        if TAG in [str(t) for t in a.tags] or isinstance(a, unreal.CoverActor):
            eas.destroy_actor(a)

    grid = next(a for a in eas.get_all_level_actors() if isinstance(a, unreal.GridManager))
    grid.modify()
    grid.set_actor_location(unreal.Vector(0, 0, 0), False, False)
    grid.set_editor_property("GridSizeX", GRID)
    grid.set_editor_property("GridSizeY", GRID)

    PLANE = unreal.load_asset("/Engine/BasicShapes/Plane")

    dirt = load_material("MI_Placeholder_Road")
    water = load_material("MI_Placeholder_Water")

    for rect in ROADS:
        flat(rect, dirt, 0.5, "Farmstead/Roads")
    for rect in PONDS:
        flat(rect, water, 0.8, "Farmstead/Ponds")

    for wr in corner_extensions(wall_runs()):
        run(wr["cls"], wr["cells"], wr["horizontal"], wr["folder"], wr["props"], wr["ext_lo"], wr["ext_hi"])
    for r, c in TREES:
        t = spawn(unreal.TreeCover, r, c)
        t.set_folder_path("Farmstead/Trees")
    for (r, c), length, along in HAY:
        run("HayBaleCover", hay_cells(r, c, length, along), along == "row", "Farmstead/Hay")

    # Check the DZ actors still cover the cells this script spawns the squads in
    # (grid cell (X, Y) from the actor = (63 - row, col) here).
    dz_cells = {}
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.DeploymentZone):
            dz_cells[a.get_editor_property("Team")] = {(GRID - 1 - p.x, p.y) for p in a.get_cells()}
    for team, cells, name in ((unreal.SquadTeam.ALLIES, ALLIED_SOLDIERS, "Allied"), (unreal.SquadTeam.AXIS, AXIS_SOLDIERS, "Axis")):
        if team not in dz_cells:
            unreal.log_warning(f"Farmstead: no {name} DeploymentZone actor in the level")
        elif not set(cells) <= dz_cells[team]:
            unreal.log_warning(f"Farmstead: {name} spawn cells are outside the {name} DeploymentZone actor")

    # Squads start in their own DZ, facing the other one (Allies face +Y / east here, Axis -Y)
    squads = {unreal.SquadTeam.ALLIES: (ALLIED_SOLDIERS, 90.0), unreal.SquadTeam.AXIS: (AXIS_SOLDIERS, -90.0)}
    placed = {team: 0 for team in squads}
    for a in sorted([a for a in eas.get_all_level_actors() if isinstance(a, unreal.Soldier)], key=lambda a: a.get_actor_label()):
        team = a.get_editor_property("Team")
        if team not in squads:
            continue
        cells, yaw = squads[team]
        if placed[team] >= len(cells):
            unreal.log_warning(f"Farmstead: extra soldier {a.get_actor_label()} has no spawn cell; left where it is")
            continue
        a.modify()
        a.set_actor_location_and_rotation(world_xy(*cells[placed[team]]), unreal.Rotator(0, 0, yaw), False, False)
        placed[team] += 1
    print("Soldiers placed:", {str(t): n for t, n in placed.items()})
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.PlayerStart):
            a.modify()
            a.set_actor_location_and_rotation(world_xy(*PLAYER_START), unreal.Rotator(0, 0, 90), False, False)

    if first_build:
        unreal.EditorLoadingAndSavingUtils.save_map(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(), MAP_PATH)
    else:
        les.save_current_level()
    print("Farmstead built:", len(eas.get_all_level_actors()), "actors")


build()
