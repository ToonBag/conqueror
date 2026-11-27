"""
Squad Command - procedural low-poly WW2 cover & environment set.

Run inside Blender (tested on 5.3), either from the Text Editor / MCP:
    exec(open(r"<this file>").read(), {"__file__": r"<this file>"})
or headless:
    blender -b --factory-startup --python build_ww2_cover.py

Builds every piece as ONE mesh object with flat-shaded faces and shared flat-colour
materials (M_Env_*), lays them out in rows by category, saves WW2_Cover_LowPoly.blend
next to this script, exports one FBX per piece to ./FBX and renders previews to ./Previews.

Conventions (match Unreal and the placeholders in Source/Unreal/CoverActor.cpp):
  * 1 Blender unit = 1 m = 1 grid cell (Unreal imports as 100 uu).
  * Pivot = centre of the piece's footprint at ground level (z = 0).
  * Wall-type pieces (walls, windows, low walls, sandbags, rubble stubs, hedges, fences)
    span exactly one cell along X (-0.5..0.5) and are centred on Y, like the placeholder slabs.
  * Furniture and vehicles face +X (imports as Unreal +X / forward).
  * Gameplay-relevant sizes follow UCoverData / CoverActor.cpp:
      full wall 2.6 m x 0.30 m, low wall 0.9 m x 0.40 m, window sill 0.9 m / top 2.0 m,
      hay stack 0.75 m, tree trunk 3.4 m (canopy ~1.7 m), rubble below 0.9 m.
  * Simple collision is exported as UCX_<Mesh>_NN boxes that match the placeholder shapes.
  * Rubble chunks (SM_Rubble_Chunk_*) are normalised to a 1 m cube centred on the pivot, so they
    are drop-in replacements for the engine cube that ABreachableWall scales per chunk.
"""
import bpy, bmesh, math, os, random
from contextlib import contextmanager
from mathutils import Vector, Matrix, Euler

try:
    HERE = os.path.dirname(os.path.abspath(__file__))
except NameError:  # pasted into the Text Editor
    HERE = os.path.dirname(bpy.data.filepath) or os.getcwd()
BLEND_PATH = os.path.join(HERE, "WW2_Cover_LowPoly.blend")
FBX_DIR = os.path.join(HERE, "FBX")
PREVIEW_DIR = os.path.join(HERE, "Previews")

# --------------------------------------------------------------------------- gameplay sizes (m)
WALL_H, WALL_T = 2.60, 0.30
LOW_H, LOW_T = 0.90, 0.40
SILL, WTOP = 0.90, 2.00
HAY_H = 0.75
TRUNK_H, TRUNK_D, CANOPY_D = 3.40, 0.35, 1.70
RUBBLE_MAX = 0.86

# --------------------------------------------------------------------------- materials
PALETTE = {  # sRGB
    "Plaster":      (0.84, 0.79, 0.68),
    "PlasterDark":  (0.62, 0.57, 0.49),   # weakened (breachable) walls, GDD 9.4
    "Brick":        (0.62, 0.31, 0.22),
    "BrickDark":    (0.44, 0.22, 0.16),
    "Stone":        (0.60, 0.58, 0.53),
    "StoneDark":    (0.43, 0.42, 0.39),
    "StoneLight":   (0.72, 0.70, 0.64),
    "Rubble":       (0.55, 0.50, 0.43),   # dust / broken masonry
    "Soot":         (0.13, 0.12, 0.11),
    "Wood":         (0.56, 0.39, 0.23),
    "WoodDark":     (0.35, 0.23, 0.14),
    "WoodLight":    (0.70, 0.54, 0.34),
    "WoodPaint":    (0.31, 0.42, 0.33),   # shutters, carts
    "Hay":          (0.87, 0.74, 0.40),
    "HayDark":      (0.68, 0.54, 0.25),
    "Bark":         (0.34, 0.25, 0.17),
    "BarkDark":     (0.20, 0.16, 0.12),
    "Leaf":         (0.34, 0.49, 0.22),
    "LeafDark":     (0.23, 0.37, 0.17),
    "LeafLight":    (0.47, 0.60, 0.28),
    "Pine":         (0.17, 0.30, 0.18),
    "Apple":        (0.72, 0.16, 0.10),
    "Iron":         (0.21, 0.21, 0.22),
    "Steel":        (0.48, 0.49, 0.50),
    "Rust":         (0.45, 0.26, 0.15),
    "VehiclePaint": (0.36, 0.38, 0.24),   # olive drab; tint per faction in Unreal
    "VehicleDark":  (0.24, 0.25, 0.17),
    "Canvas":       (0.55, 0.52, 0.39),
    "Rubber":       (0.09, 0.09, 0.09),
    "Glass":        (0.36, 0.46, 0.50),
    "Water":        (0.15, 0.24, 0.26),
    "Sandbag":      (0.67, 0.60, 0.44),
    "Fabric":       (0.86, 0.83, 0.75),
    "FabricRed":    (0.55, 0.19, 0.16),
    "FabricBlue":   (0.22, 0.30, 0.45),
    "FabricGreen":  (0.30, 0.40, 0.26),
    "Brass":        (0.80, 0.62, 0.30),
}
METALLIC = {"Iron": 0.5, "Steel": 0.8, "Brass": 0.7, "Rust": 0.2}
ROUGHNESS = {"Glass": 0.15, "Water": 0.1, "Steel": 0.45, "Brass": 0.4}
MATS = {}


def _lin(c):
    return ((c + 0.055) / 1.055) ** 2.4 if c > 0.04045 else c / 12.92


def make_materials():
    for key, srgb in PALETTE.items():
        name = "M_Env_" + key
        mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
        lin = tuple(_lin(c) for c in srgb) + (1.0,)
        mat.diffuse_color = lin
        mat.roughness = ROUGHNESS.get(key, 0.85)
        mat.metallic = METALLIC.get(key, 0.0)
        if mat.node_tree is None and hasattr(mat, "use_nodes"):
            mat.use_nodes = True
        bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if bsdf:
            bsdf.inputs["Base Color"].default_value = lin
            bsdf.inputs["Roughness"].default_value = ROUGHNESS.get(key, 0.85)
            bsdf.inputs["Metallic"].default_value = METALLIC.get(key, 0.0)
        MATS[key] = mat


# --------------------------------------------------------------------------- builder
def _basis(axis):
    axis = Vector(axis).normalized()
    ref = Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((1, 0, 0))
    u = axis.cross(ref).normalized()
    return axis, u, axis.cross(u)


def _ico_bm(sub):
    tmp = bmesh.new()
    try:
        bmesh.ops.create_icosphere(tmp, subdivisions=sub, radius=1.0)
    except TypeError:
        bmesh.ops.create_icosphere(tmp, subdivisions=sub, diameter=2.0)
    return tmp


BOX_FACES = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]


class Mesh:
    """Accumulates primitives into a single bmesh; finish() turns it into an object."""

    def __init__(self, name, seed=None):
        self.name, self.bm, self.mats, self.cols = name, bmesh.new(), [], []
        self.r = random.Random(seed if seed is not None else name)
        self.stack = [Matrix.Identity(4)]

    # ---- helpers
    def _mi(self, m):
        if m not in self.mats:
            self.mats.append(m)
        return self.mats.index(m)

    def _v(self, p):
        return self.bm.verts.new(self.stack[-1] @ Vector(p))

    def _face(self, verts, m):
        f = self.bm.faces.new(verts)
        f.material_index = self._mi(m)
        f.smooth = False
        return f

    def j(self, a):
        return Vector((self.r.uniform(-a, a), self.r.uniform(-a, a), self.r.uniform(-a, a)))

    def pick(self, *opts):
        return self.r.choice(opts)

    @contextmanager
    def xf(self, loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1)):
        if not isinstance(scale, (tuple, list)):
            scale = (scale, scale, scale)
        M = Matrix.Translation(loc) @ Euler(rot).to_matrix().to_4x4() @ Matrix.Diagonal((*scale, 1.0))
        self.stack.append(self.stack[-1] @ M)
        try:
            yield
        finally:
            self.stack.pop()

    # ---- primitives
    def box(self, x0, x1, y0, y1, z0, z1, m, jit=0.0, top_scale=1.0):
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        pts = []
        for k, z in enumerate((z0, z1)):
            s = 1.0 if k == 0 else top_scale
            for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
                p = Vector((cx + (x - cx) * s, cy + (y - cy) * s, z))
                if jit:
                    p += self.j(jit)
                pts.append(p)
        v = [self._v(p) for p in pts]
        for f in BOX_FACES:
            self._face([v[i] for i in f], m)

    def boxc(self, c, size, m, rz=0.0, jit=0.0, rot=None, top_scale=1.0):
        sx, sy, sz = size
        with self.xf(c, rot if rot else (0, 0, rz)):
            self.box(-sx / 2, sx / 2, -sy / 2, sy / 2, -sz / 2, sz / 2, m, jit, top_scale)

    def beam(self, p0, p1, w, h, m):
        """Box of width w (side) and height h (up) running from p0 to p1."""
        p0, p1 = Vector(p0), Vector(p1)
        x, u, v = _basis(p1 - p0)
        L = (p1 - p0).length
        pts = [p0 + x * a + u * (b * w / 2) + v * (c * h / 2)
               for a in (0, L) for (b, c) in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        # reorder to BOX_FACES layout (bottom ring then top ring)
        ring0 = [pts[0], pts[4], pts[5], pts[1]]
        ring1 = [pts[3], pts[7], pts[6], pts[2]]
        vv = [self._v(p) for p in ring0 + ring1]
        for f in BOX_FACES:
            self._face([vv[i] for i in f], m)

    def prism(self, pts, d0, d1, m, plane="xz"):
        """Polygon profile [(u, v)] extruded between d0..d1.
        'xz': (x, z) along y; 'xy': (x, y) along z; 'yz': (y, z) along x."""
        def P(u, v, d):
            return {"xz": (u, d, v), "xy": (u, v, d), "yz": (d, u, v)}[plane]
        a = [self._v(P(u, v, d0)) for u, v in pts]
        b = [self._v(P(u, v, d1)) for u, v in pts]
        self._face(a[::-1], m)
        self._face(b, m)
        n = len(pts)
        for i in range(n):
            k = (i + 1) % n
            self._face([a[i], a[k], b[k], b[i]], m)

    def lathe(self, prof, p0, axis, m, seg=8, rot=None, jit=0.0):
        """Revolve [(t, r)] around axis starting at p0. r == 0 makes a pole."""
        axis, u, v = _basis(axis)
        p0 = Vector(p0)
        rot = math.pi / seg if rot is None else rot
        rings = []
        for t, r in prof:
            c = p0 + axis * t
            if r < 1e-6:
                rings.append([self._v(c)])
            else:
                rings.append([self._v(c + (u * math.cos(rot + 2 * math.pi * i / seg) +
                                           v * math.sin(rot + 2 * math.pi * i / seg)) * r +
                                      (self.j(jit) if jit else Vector()))
                              for i in range(seg)])
        for k in range(len(rings) - 1):
            A, B = rings[k], rings[k + 1]
            if len(A) == 1 and len(B) == 1:
                continue
            for i in range(seg):
                n = (i + 1) % seg
                if len(A) == 1:
                    self._face([A[0], B[i], B[n]], m)
                elif len(B) == 1:
                    self._face([A[i], A[n], B[0]], m)
                else:
                    self._face([A[i], A[n], B[n], B[i]], m)
        if len(rings[0]) > 1:
            self._face(rings[0][::-1], m)
        if len(rings[-1]) > 1:
            self._face(rings[-1], m)

    def cyl(self, p0, p1, r, m, seg=8, r1=None, rot=None):
        p0, p1 = Vector(p0), Vector(p1)
        self.lathe([(0, r), ((p1 - p0).length, r if r1 is None else r1)], p0, p1 - p0, m, seg, rot)

    def ico(self, c, r, m, sub=2, scale=(1, 1, 1), jit=0.0, flat_bottom=None):
        tmp = _ico_bm(sub)
        vmap = {}
        for v in tmp.verts:
            p = Vector((v.co.x * scale[0], v.co.y * scale[1], v.co.z * scale[2])) * r
            if jit:
                p += self.j(jit * r)
            p += Vector(c)
            if flat_bottom is not None:
                p.z = max(p.z, flat_bottom)
            vmap[v] = self._v(p)
        for f in tmp.faces:
            self._face([vmap[v] for v in f.verts], m)
        tmp.free()

    def bag(self, c, L, W, H, m, rz=0.0, jit=0.0):
        """Pillow-shaped sack lying along local X, bottom at c.z."""
        ch = 0.25
        def ring(sx, sy, z):
            hx, hy = L / 2 * sx, W / 2 * sy
            cx, cy = hx * ch * 2, hy * ch * 2
            return [(hx - cx, -hy, z), (hx, -hy + cy, z), (hx, hy - cy, z), (hx - cx, hy, z),
                    (-hx + cx, hy, z), (-hx, hy - cy, z), (-hx, -hy + cy, z), (-hx + cx, -hy, z)]
        with self.xf(c, (0, 0, rz)):
            rings = [[self._v(Vector(p) + (self.j(jit) if jit else Vector())) for p in ring(*s)]
                     for s in ((0.86, 0.80, 0.0), (1.0, 1.0, H * 0.5), (0.86, 0.75, H))]
        for k in range(2):
            A, B = rings[k], rings[k + 1]
            for i in range(8):
                n = (i + 1) % 8
                self._face([A[i], A[n], B[n], B[i]], m)
        self._face(rings[0][::-1], m)
        self._face(rings[2], m)

    def collide(self, x0, x1, y0, y1, z0, z1):
        self.cols.append((x0, x1, y0, y1, z0, z1))

    # ---- post
    def clamp_z(self, zmax, zmin=0.0):
        for v in self.bm.verts:
            v.co.z = min(max(v.co.z, zmin), zmax)

    def normalize(self):
        """Centre on the pivot and scale to a 1 m cube (for rubble chunks)."""
        cs = [v.co for v in self.bm.verts]
        mn = Vector((min(c.x for c in cs), min(c.y for c in cs), min(c.z for c in cs)))
        mx = Vector((max(c.x for c in cs), max(c.y for c in cs), max(c.z for c in cs)))
        ctr, size = (mn + mx) / 2, mx - mn
        for v in self.bm.verts:
            d = v.co - ctr
            v.co = Vector((d.x / size.x, d.y / size.y, d.z / size.z))

    def finish(self, coll, col_coll):
        bm = self.bm
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
        ngons = [f for f in bm.faces if len(f.verts) > 4]
        bmesh.ops.triangulate(bm, faces=ngons, quad_method="BEAUTY", ngon_method="EAR_CLIP")
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        # world-scale box-projected UVs (1 UV unit = 1 m) so tiling textures can come later
        uv = bm.loops.layers.uv.new("UVMap")
        for f in bm.faces:
            n = f.normal
            ax = max(range(3), key=lambda i: abs(n[i]))
            for l in f.loops:
                co = l.vert.co
                l[uv].uv = {0: (co.y, co.z), 1: (co.x, co.z), 2: (co.x, co.y)}[ax]
        me = bpy.data.meshes.new(self.name)
        bm.to_mesh(me)
        bm.free()
        for m in self.mats:
            me.materials.append(MATS[m])
        ob = bpy.data.objects.new(self.name, me)
        coll.objects.link(ob)
        ob["collision"] = []
        for i, (x0, x1, y0, y1, z0, z1) in enumerate(self.cols):
            cb = bmesh.new()
            v = [cb.verts.new(p) for p in ((x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                                           (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))]
            for f in BOX_FACES:
                cb.faces.new([v[k] for k in f])
            bmesh.ops.recalc_face_normals(cb, faces=cb.faces)
            cme = bpy.data.meshes.new("UCX_%s_%02d" % (self.name, i))
            cb.to_mesh(cme)
            cb.free()
            co = bpy.data.objects.new(cme.name, cme)
            co.display_type = "WIRE"
            co.hide_render = True
            co["collision_for"] = self.name
            col_coll.objects.link(co)
        return ob


# =========================================================================== WALLS
def _brick_patch(m, cx, cz, w, h, side, face=0.14):
    """Plaster knocked off, bricks showing, on the face at y = side * face."""
    y0, y1 = sorted((side * face, side * (face + 0.006)))
    m.box(cx - w / 2, cx + w / 2, y0, y1, cz - h / 2, cz + h / 2, "BrickDark")
    yb0, yb1 = sorted((side * face, side * (face + 0.011)))
    rows = max(1, int(h / 0.075))
    for r in range(rows):
        z = cz - h / 2 + 0.01 + r * 0.075
        off = 0.0 if r % 2 == 0 else 0.11
        x = cx - w / 2 + 0.01 - off
        while x < cx + w / 2 - 0.05:
            xa, xb = max(x, cx - w / 2 + 0.01), min(x + 0.20, cx + w / 2 - 0.01)
            if xb - xa > 0.05 and m.r.random() > 0.15:
                m.box(xa, xb, yb0, yb1, z, min(z + 0.06, cz + h / 2 - 0.005), "Brick")
            x += 0.22


def _wall_shell(m, body, x0=-0.5, x1=0.5, z0=0.0, z1=WALL_H, plinth=True, coping=True):
    """Plinth (full thickness), body inset 1 cm per face, coping cap (full thickness)."""
    t = WALL_T / 2
    zb = z0
    if plinth and z0 < 0.35:
        m.box(x0, x1, -t, t, z0, 0.35, "StoneDark")
        zb = 0.35
    zt = z1 - 0.08 if coping and z1 >= WALL_H - 1e-4 else z1
    if zt > zb:
        m.box(x0, x1, -0.14, 0.14, zb, zt, body)
    if coping and z1 >= WALL_H - 1e-4:
        m.box(x0, x1, -t, t, WALL_H - 0.08, WALL_H, "Stone")


def wall_plaster(name="SM_Wall_Plaster", patched=False):
    """Clean plaster wall; the _Patched variant has plaster knocked off showing brick (mix ~1 in 3)."""
    m = Mesh(name)
    _wall_shell(m, "Plaster")
    if patched:
        for side in (-1, 1):
            _brick_patch(m, m.r.uniform(-0.2, 0.2), m.r.uniform(0.9, 1.9),
                         m.r.uniform(0.34, 0.5), m.r.uniform(0.24, 0.36), side)
    m.collide(-0.5, 0.5, -WALL_T / 2, WALL_T / 2, 0, WALL_H)
    return m


def wall_brick(name="SM_Wall_Brick"):
    m = Mesh(name)
    t = WALL_T / 2
    m.box(-0.5, 0.5, -t, t, 0, 0.30, "StoneDark")
    m.box(-0.5, 0.5, -0.14, 0.14, 0.30, WALL_H - 0.08, "Brick")
    m.box(-0.5, 0.5, -t, t, WALL_H - 0.08, WALL_H, "Stone")
    for side in (-1, 1):
        y0, y1 = sorted((side * 0.14, side * 0.148))
        # decorative darker band (header course) and a scatter of proud / weathered bricks
        m.box(-0.5, 0.5, y0, y1, 2.20, 2.27, "BrickDark")
        for _ in range(9):
            r = m.r.randint(0, 26)
            z = 0.32 + r * 0.075
            x = -0.5 + m.r.randint(0, 3) * 0.22 + (0.11 if r % 2 else 0) + 0.01
            xb = min(x + 0.20, 0.5)
            if xb - x > 0.06 and z < 2.15:
                m.box(x, xb, y0, y1, z, z + 0.065, m.pick("BrickDark", "Brick", "BrickDark"))
    m.collide(-0.5, 0.5, -t, t, 0, WALL_H)
    return m


def _crack(m, side, start, segs, face=0.14):
    y = side * (face + 0.004)
    p = Vector((start[0], y, start[1]))
    for _ in range(segs):
        a = math.radians(m.r.uniform(50, 130))
        L = m.r.uniform(0.2, 0.38)
        q = p + Vector((math.cos(a) * L, 0, math.sin(a) * L))
        q.x = max(-0.46, min(0.46, q.x))
        if q.z > WALL_H - 0.2:
            break
        m.beam(p, q, 0.01, 0.028, "Soot")
        p = q


def wall_weakened(name="SM_Wall_Plaster_Weakened"):
    """GDD 9.4: darker weathered plaster, dark cracks up both faces, chipped patches, notch in the top."""
    m = Mesh(name)
    t = WALL_T / 2
    nx0, nx1 = sorted((m.r.uniform(-0.3, -0.05), m.r.uniform(0.08, 0.3)))
    nz = WALL_H - 0.24
    m.box(-0.5, 0.5, -t, t, 0, 0.35, "StoneDark")
    m.box(-0.5, nx0, -0.14, 0.14, 0.35, WALL_H - 0.08, "PlasterDark")
    m.box(nx1, 0.5, -0.14, 0.14, 0.35, WALL_H - 0.08, "PlasterDark")
    m.box(nx0, nx1, -0.14, 0.14, 0.35, nz, "PlasterDark")
    m.box(-0.5, nx0, -t, t, WALL_H - 0.08, WALL_H, "StoneDark")
    m.box(nx1, 0.5, -t, t, WALL_H - 0.08, WALL_H, "StoneDark")
    # broken brick in the notch
    for i in range(3):
        x = nx0 + (nx1 - nx0) * (i + 0.5) / 3
        m.boxc((x, 0, nz + 0.03), (0.12, 0.24, 0.07), "Brick", rz=m.r.uniform(-0.3, 0.3), jit=0.012)
    for side in (-1, 1):
        for k in range(2):
            _crack(m, side, (m.r.uniform(-0.35, 0.35), 0.36), 6)
        _brick_patch(m, m.r.uniform(-0.25, 0.25), m.r.uniform(1.3, 2.0), 0.30, 0.22, side)
        _brick_patch(m, m.r.uniform(-0.3, 0.3), m.r.uniform(0.55, 0.9), 0.24, 0.16, side)
    m.collide(-0.5, 0.5, -t, t, 0, WALL_H)
    return m


def wall_pillar(name, body):
    """0.3 x 0.3 corner / end post. Place at wall junctions and free wall ends."""
    m = Mesh(name)
    t = WALL_T / 2
    m.box(-t - 0.01, t + 0.01, -t - 0.01, t + 0.01, 0, 0.35, "StoneDark")
    m.box(-t, t, -t, t, 0.35, WALL_H - 0.08, body)
    m.box(-t - 0.01, t + 0.01, -t - 0.01, t + 0.01, WALL_H - 0.08, WALL_H, "Stone")
    # quoin stones up the corners
    if body == "Plaster":
        for i in range(6):
            z = 0.45 + i * 0.34
            if i % 2:
                m.box(-t - 0.006, t + 0.006, -t - 0.006, 0.02, z, z + 0.2, "StoneLight")
            else:
                m.box(-t - 0.006, 0.02, -t - 0.006, t + 0.006, z, z + 0.2, "StoneLight")
    m.collide(-t, t, -t, t, 0, WALL_H)
    return m


def wall_wood(name="SM_Wall_Wood"):
    """Full-height plank stockade (Small House's yard walls): vertical boards with a rail near the top and bottom on
    both faces. A full wall in play (2.6 m, blocks sight); the boards are thinner than WALL_T, the rails fill it out."""
    m = Mesh(name)
    n = 5
    for i in range(n):
        x0 = -0.5 + i / n
        x1 = x0 + 1.0 / n - 0.012
        h = WALL_H - m.r.uniform(0.0, 0.09)
        m.box(x0 + 0.006, x1, -0.06, 0.06, 0, h, m.pick("Wood", "WoodDark", "Wood", "WoodDark", "WoodLight"), jit=0.004)
    for z in (0.35, 2.05):
        for side in (-1, 1):
            y0, y1 = sorted((side * 0.06, side * 0.13))
            m.box(-0.5, 0.5, y0, y1, z, z + 0.15, "WoodDark", jit=0.004)
    m.collide(-0.5, 0.5, -WALL_T / 2, WALL_T / 2, 0, WALL_H)
    return m


def wall_wood_window(name="SM_Wall_Wood_Window"):
    """Plank stockade with an unglazed window cut through it: boards below the 0.9 m sill and above the 2.0 m top,
    a post either side of the opening and a board sill / lintel on both faces."""
    m = Mesh(name)
    t = WALL_T / 2
    n = 5
    for i in range(n):
        x0 = -0.5 + i / n
        x1 = x0 + 1.0 / n - 0.012
        mat = m.pick("Wood", "WoodDark", "Wood", "WoodDark", "WoodLight")
        m.box(x0 + 0.006, x1, -0.06, 0.06, 0, SILL, mat, jit=0.004)
        if i in (0, n - 1):  # the outer boards run full height: posts either side of the opening
            m.box(x0 + 0.006, x1, -0.06, 0.06, SILL, WTOP, mat, jit=0.004)
        m.box(x0 + 0.006, x1, -0.06, 0.06, WTOP, WALL_H - m.r.uniform(0.0, 0.09), mat, jit=0.004)
    for side in (-1, 1):
        y0, y1 = sorted((side * 0.06, side * 0.13))
        m.box(-0.5, 0.5, y0, y1, 0.35, 0.50, "WoodDark", jit=0.004)            # bottom rail
        m.box(-0.5, 0.5, y0, y1, SILL - 0.05, SILL, "WoodDark")                 # sill board
        m.box(-0.5, 0.5, y0, y1, WTOP, WTOP + 0.12, "WoodDark")                 # lintel board
        m.box(-0.5, 0.5, y0, y1, 2.05, 2.20, "WoodDark", jit=0.004)            # top rail
    m.collide(-0.5, 0.5, -t, t, 0, SILL)
    m.collide(-0.5, 0.5, -t, t, WTOP, WALL_H)
    m.collide(-0.5, -0.3, -t, t, SILL, WTOP)
    m.collide(0.3, 0.5, -t, t, SILL, WTOP)
    return m


def wall_doorway(name="SM_Wall_Doorway_Plaster"):
    """Cosmetic lintel + frame over an open (walkable) doorway cell."""
    m = Mesh(name)
    t = WALL_T / 2
    _wall_shell(m, "Plaster", z0=2.15, plinth=False)
    m.box(-0.5, 0.5, -0.15, 0.15, 2.05, 2.15, "WoodDark")
    for s in (-1, 1):
        x0, x1 = sorted((s * 0.5, s * 0.43))
        m.box(x0, x1, -0.15, 0.15, 0, 2.05, "WoodDark")
    m.box(-0.43, 0.43, -0.04, 0.04, 0, 0.03, "StoneDark")  # threshold
    m.collide(-0.5, 0.5, -t, t, 2.05, WALL_H)
    m.collide(-0.5, -0.43, -t, t, 0, 2.05)
    m.collide(0.43, 0.5, -t, t, 0, 2.05)
    return m


def wall_window(name, body, broken=False):
    m = Mesh(name)
    t = WALL_T / 2
    # lower wall (to the sill) and upper wall (above the opening)
    m.box(-0.5, 0.5, -t, t, 0, 0.35, "StoneDark")
    m.box(-0.5, 0.5, -0.14, 0.14, 0.35, SILL - 0.05, body)
    m.box(-0.5, 0.5, -0.17, 0.17, SILL - 0.05, SILL, "StoneLight")          # stone sill
    m.box(-0.5, 0.5, -0.15, 0.15, WTOP, WTOP + 0.12, "WoodDark")           # timber lintel
    m.box(-0.5, 0.5, -0.14, 0.14, WTOP + 0.12, WALL_H - 0.08, body)
    m.box(-0.5, 0.5, -t, t, WALL_H - 0.08, WALL_H, "Stone")
    # reveals either side of the opening (same 12 cm posts as the placeholder)
    for s in (-1, 1):
        x0, x1 = sorted((s * 0.5, s * 0.38))
        m.box(x0, x1, -0.14, 0.14, SILL, WTOP, body)
    # timber window frame set in the middle of the wall
    fy = 0.035
    m.box(-0.38, -0.33, -fy, fy, SILL, WTOP, "Wood")
    m.box(0.33, 0.38, -fy, fy, SILL, WTOP, "Wood")
    m.box(-0.38, 0.38, -fy, fy, WTOP - 0.05, WTOP, "Wood")
    m.box(-0.38, 0.38, -fy, fy, SILL, SILL + 0.04, "Wood")
    if not broken:
        m.box(-0.33, 0.33, -fy, fy, 1.72, 1.76, "Wood")                     # transom
        m.box(-0.33, 0.33, -0.006, 0.006, 1.76, WTOP - 0.05, "Glass")       # fanlight glass
        m.box(-0.015, 0.015, -fy, fy, 1.76, WTOP - 0.05, "Wood")
        if body == "Plaster":
            # open shutters folded back into the reveals' outer face
            for s in (-1, 1):
                x0, x1 = sorted((s * 0.38, s * 0.36))
                m.box(x0, x1, 0.06, 0.15, SILL + 0.06, WTOP - 0.04, "WoodPaint")
    else:
        # snapped transom, glass shards, chunks on the sill, bullet pocks
        m.beam((-0.33, 0, 1.74), (0.05, 0, 1.62), 0.06, 0.04, "Wood")
        m.beam((0.33, 0, 1.74), (0.18, 0, 1.80), 0.06, 0.04, "Wood")
        for _ in range(5):
            x = m.r.uniform(-0.3, 0.3)
            z = m.pick(SILL + 0.04, WTOP - 0.05)
            h = m.r.uniform(0.06, 0.16) * (1 if z < 1.5 else -1)
            a = m._v((x - 0.04, 0, z)); b = m._v((x + 0.04, 0, z)); c = m._v((x + m.r.uniform(-0.03, 0.03), 0, z + h))
            m._face([a, b, c], "Glass")
        for _ in range(4):
            m.boxc((m.r.uniform(-0.3, 0.3), m.r.uniform(-0.1, 0.1), SILL + 0.03),
                   (m.r.uniform(0.05, 0.1),) * 3, m.pick(body, "Brick"), rz=m.r.uniform(0, 3), jit=0.01)
        for side in (-1, 1):
            for _ in range(8):
                x = m.pick(m.r.uniform(-0.48, -0.4), m.r.uniform(0.4, 0.48), m.r.uniform(-0.4, 0.4))
                z = m.r.uniform(0.5, 0.8) if m.r.random() < 0.5 else m.r.uniform(2.15, 2.45)
                y0, y1 = sorted((side * 0.14, side * 0.146))
                m.box(x - 0.025, x + 0.025, y0, y1, z - 0.025, z + 0.025, "Soot", jit=0.004)
            _brick_patch(m, m.r.uniform(-0.2, 0.2), 0.62, 0.26, 0.14, side)
    m.collide(-0.5, 0.5, -t, t, 0, SILL)
    m.collide(-0.5, 0.5, -t, t, WTOP, WALL_H)
    m.collide(-0.5, -0.38, -t, t, SILL, WTOP)
    m.collide(0.38, 0.5, -t, t, SILL, WTOP)
    return m


# =========================================================================== LOW WALLS
def lowwall_stone(name="SM_LowWall_Stone", damaged=False):
    m = Mesh(name)
    hw = LOW_T / 2
    m.box(-0.5, 0.5, -0.16, 0.16, 0, 0.80, "StoneDark")
    gap = (m.r.uniform(-0.3, 0.1), 0.0) if damaged else None
    if gap:
        gap = (gap[0], gap[0] + 0.32)
    for side in (-1, 1):
        for row in range(3):
            z0 = row * 0.27
            x = -0.5 + (0 if row % 2 == 0 else -0.12)
            while x < 0.5:
                w = m.r.uniform(0.18, 0.32)
                xa, xb = max(x, -0.5), min(x + w, 0.5)
                if gap and row == 2 and xb > gap[0] and xa < gap[1]:
                    x += w
                    continue
                if xb - xa > 0.06:
                    y0, y1 = sorted((side * 0.15, side * (hw - 0.012)))
                    m.box(xa + 0.012, xb - 0.012, y0, y1, z0 + 0.012, z0 + m.r.uniform(0.23, 0.26),
                          m.pick("Stone", "Stone", "StoneDark", "StoneLight"), jit=0.011)
                x += w
    # capstones set on edge
    x = -0.5
    while x < 0.49:
        w = m.r.uniform(0.11, 0.16)
        xb = min(x + w, 0.5)
        if not (gap and xb > gap[0] and x < gap[1]):
            m.box(x + 0.01, xb - 0.01, -hw + 0.012, hw - 0.012, 0.78, LOW_H - 0.012,
                  m.pick("Stone", "StoneLight"), jit=0.01)
        x += w
    if gap:
        # stones knocked off the top lie at the foot of the wall
        for _ in range(4):
            s = m.pick(-1, 1)
            m.boxc((m.r.uniform(gap[0], gap[1]), s * m.r.uniform(0.27, 0.42), 0.06),
                   (m.r.uniform(0.14, 0.24), m.r.uniform(0.1, 0.16), 0.1), m.pick("Stone", "StoneDark"),
                   rz=m.r.uniform(0, 3), jit=0.012)
    m.clamp_z(LOW_H)
    m.collide(-0.5, 0.5, -hw, hw, 0, LOW_H)
    return m


def lowwall_brick(name="SM_LowWall_Brick"):
    m = Mesh(name)
    hw = LOW_T / 2
    m.box(-0.5, 0.5, -0.18, 0.18, 0, 0.80, "Brick")
    m.box(-0.5, 0.5, -hw, hw, 0.80, LOW_H, "StoneLight")
    for side in (-1, 1):
        y0, y1 = sorted((side * 0.18, side * 0.187))
        m.box(-0.5, 0.5, y0, y1, 0, 0.12, "BrickDark")
        for _ in range(5):
            r = m.r.randint(2, 9)
            z = r * 0.075
            x = -0.5 + m.r.randint(0, 3) * 0.22 + (0.11 if r % 2 else 0) + 0.01
            xb = min(x + 0.2, 0.5)
            if xb - x > 0.06:
                m.box(x, xb, y0, y1, z, z + 0.065, "BrickDark")
    m.collide(-0.5, 0.5, -hw, hw, 0, LOW_H)
    return m


def sandbags(name="SM_Sandbags"):
    """Sandbag wall, 5 courses = 0.9 m, interlocking stretchers. Low-wall cover."""
    m = Mesh(name)
    H = LOW_H / 5
    for row in range(5):
        z = row * H
        w = 0.42 - row * 0.02
        if row % 2 == 0:
            xs = [(-0.25, 0.5), (0.25, 0.5)]
        else:
            xs = [(-0.375, 0.25), (0.0, 0.5), (0.375, 0.25)]
        for x, L in xs:
            m.bag((x + m.r.uniform(-0.01, 0.01), m.r.uniform(-0.01, 0.01), z), L * 0.98, w, H * 1.05,
                  "Sandbag", rz=m.r.uniform(-0.04, 0.04), jit=0.008)
    m.clamp_z(LOW_H)
    m.collide(-0.5, 0.5, -0.22, 0.22, 0, LOW_H)
    return m


# =========================================================================== HAY
def _bale(m, c, rz=0.0, L=0.92, W=0.42, H=HAY_H / 2, tilt=0.0):
    ch = 0.045
    prof = [(-W / 2 + ch, 0), (W / 2 - ch, 0), (W / 2, ch), (W / 2, H - ch), (W / 2 - ch, H),
            (-W / 2 + ch, H), (-W / 2, H - ch), (-W / 2, ch)]
    band = [(y + (0.006 if y > 0 else -0.006), z + (0.006 if z > H / 2 else 0)) for y, z in prof]
    with m.xf(c, (tilt, 0, rz)):
        m.prism(prof, -L / 2, L / 2, "Hay", "yz")
        for bx in (-L * 0.28, L * 0.28):
            m.prism(band, bx - 0.012, bx + 0.012, "HayDark", "yz")


def hay_stack(name="SM_HayBale_Stack", variant=0):
    """Two bales side by side and one on top: 0.75 m (UCoverData.HayBaleHeight)."""
    m = Mesh(name)
    if variant == 0:
        _bale(m, (0, -0.22, 0), m.r.uniform(-0.03, 0.03))
        _bale(m, (0, 0.22, 0), m.r.uniform(-0.03, 0.03))
        _bale(m, (0.02, 0.01, HAY_H / 2), m.r.uniform(-0.08, 0.08))
    else:
        _bale(m, (-0.24, 0, 0), math.pi / 2 + m.r.uniform(-0.04, 0.04))
        _bale(m, (0.24, 0, 0), math.pi / 2 + m.r.uniform(-0.04, 0.04))
        _bale(m, (0.0, 0.0, HAY_H / 2), m.r.uniform(-0.1, 0.1))
    for _ in range(5):  # loose straw on the ground
        x, y = m.r.uniform(-0.4, 0.4), m.pick(-0.42, 0.42) * m.r.uniform(0.9, 1.0)
        m.boxc((x, y, 0.01), (m.r.uniform(0.1, 0.2), 0.08, 0.02), "HayDark", rz=m.r.uniform(0, 3), jit=0.01)
    m.collide(-0.46, 0.46, -0.44, 0.44, 0, HAY_H)
    return m


def hay_single(name="SM_HayBale_Single"):
    m = Mesh(name)
    _bale(m, (0, 0, 0))
    m.collide(-0.46, 0.46, -0.21, 0.21, 0, HAY_H / 2)
    return m


def hay_loose(name="SM_HayPile_Loose"):
    m = Mesh(name)
    m.ico((0, 0, -0.15), 0.5, "Hay", sub=2, scale=(1.0, 0.9, 0.9), jit=0.08, flat_bottom=0.0)
    for _ in range(5):
        a = m.r.uniform(0, 6.28)
        m.ico((math.cos(a) * 0.28, math.sin(a) * 0.25, -0.05), 0.22, m.pick("Hay", "HayDark"), sub=1,
              scale=(1.2, 1, 0.8), jit=0.1, flat_bottom=0.0)
    m.ico((0, 0, 0.25), 0.25, "HayDark", sub=1, scale=(1.2, 1, 1), jit=0.1)
    m.clamp_z(HAY_H)
    m.collide(-0.45, 0.45, -0.45, 0.45, 0, HAY_H * 0.9)
    return m


# =========================================================================== TREES
def _trunk(m, h, r0, r1, mat="Bark", seg=7, lean=(0, 0)):
    m.lathe([(0, r0 * 1.45), (0.18, r0 * 1.05), (0.5, r0), (h, r1)], (0, 0, 0),
            (lean[0], lean[1], 1.0), mat, seg, jit=r0 * 0.06)


def _canopy(m, centre, blobs, mats=("Leaf", "LeafDark", "LeafLight"), sub=2, jit=0.14):
    cx, cy, cz = centre
    for (dx, dy, dz, r, sc) in blobs:
        m.ico((cx + dx, cy + dy, cz + dz), r, m.pick(*mats), sub=sub, scale=sc, jit=jit)


def tree_oak(name="SM_Tree_Oak"):
    m = Mesh(name)
    _trunk(m, TRUNK_H, TRUNK_D / 2, 0.11)
    for a, z, L in ((0.6, 2.4, 0.75), (2.8, 2.7, 0.7), (4.5, 3.0, 0.6)):
        m.cyl((0, 0, z), (math.cos(a) * L, math.sin(a) * L, z + 0.65), 0.07, "Bark", seg=5, r1=0.035)
    c = (0, 0, TRUNK_H + 0.45)
    _canopy(m, c, [(0, 0, 0.15, 0.78, (1, 1, 0.85)),
                   (0.5, 0.15, -0.15, 0.55, (1, 1, 0.8)), (-0.45, 0.3, -0.1, 0.55, (1, 1, 0.8)),
                   (-0.1, -0.5, -0.2, 0.52, (1, 1, 0.8)), (0.2, 0.0, 0.6, 0.5, (1, 1, 0.8))])
    m.collide(-TRUNK_D / 2, TRUNK_D / 2, -TRUNK_D / 2, TRUNK_D / 2, 0, TRUNK_H)
    m.collide(-0.85, 0.85, -0.85, 0.85, TRUNK_H, TRUNK_H + 1.3)
    return m


def tree_apple(name="SM_Tree_Apple"):
    """Orchard fruit tree: short trunk splitting into limbs, broad low canopy with apples."""
    m = Mesh(name)
    _trunk(m, 1.6, 0.16, 0.12, lean=(0.05, 0.02))
    for a in (0.3, 2.4, 4.4):
        m.cyl((0.04, 0.0, 1.5), (math.cos(a) * 0.65, math.sin(a) * 0.65, 2.7), 0.09, "Bark", seg=5, r1=0.05)
    c = (0.0, 0.0, 3.05)
    _canopy(m, c, [(0, 0, 0.1, 0.75, (1.25, 1.25, 0.75)), (0.6, 0.2, -0.15, 0.5, (1, 1, 0.8)),
                   (-0.55, 0.35, -0.1, 0.5, (1, 1, 0.8)), (0.05, -0.6, -0.2, 0.5, (1, 1, 0.8)),
                   (-0.1, 0.1, 0.5, 0.48, (1.1, 1.1, 0.7))], mats=("Leaf", "LeafLight"))
    for _ in range(12):
        a = m.r.uniform(0, 6.28)
        rr = m.r.uniform(0.75, 0.95)
        m.ico((math.cos(a) * rr, math.sin(a) * rr, c[2] + m.r.uniform(-0.45, 0.25)), 0.045, "Apple", sub=1)
    m.collide(-TRUNK_D / 2, TRUNK_D / 2, -TRUNK_D / 2, TRUNK_D / 2, 0, 2.4)
    m.collide(-0.9, 0.9, -0.9, 0.9, 2.4, 3.8)
    return m


def tree_poplar(name="SM_Tree_Poplar"):
    m = Mesh(name)
    _trunk(m, 4.5, 0.15, 0.06)
    _canopy(m, (0, 0, 3.9), [(0, 0, 0, 0.75, (0.85, 0.85, 2.6)), (0.15, 0.1, -1.0, 0.5, (1, 1, 1.4)),
                             (-0.12, -0.1, 1.1, 0.45, (1, 1, 1.6))], mats=("Leaf", "LeafDark"))
    m.collide(-TRUNK_D / 2, TRUNK_D / 2, -TRUNK_D / 2, TRUNK_D / 2, 0, 2.0)
    m.collide(-0.65, 0.65, -0.65, 0.65, 2.0, 5.8)
    return m


def tree_pine(name="SM_Tree_Pine"):
    m = Mesh(name)
    _trunk(m, 5.6, 0.16, 0.05, mat="BarkDark")
    for z, r, h in ((1.4, 1.05, 1.6), (2.4, 0.9, 1.5), (3.4, 0.7, 1.4), (4.4, 0.48, 1.4)):
        m.lathe([(0, r * 0.55), (0.25, r), (h, 0)], (0, 0, z), (0, 0, 1), m.pick("Pine", "LeafDark"),
                seg=7, rot=m.r.uniform(0, 1), jit=0.05)
    m.collide(-TRUNK_D / 2, TRUNK_D / 2, -TRUNK_D / 2, TRUNK_D / 2, 0, TRUNK_H)
    m.collide(-0.9, 0.9, -0.9, 0.9, 1.4, 5.8)
    return m


def tree_dead(name="SM_Tree_Shattered"):
    """Shell-blasted trunk: snapped top, a few bare branches."""
    m = Mesh(name)
    h, r0, r1 = 2.9, TRUNK_D / 2, 0.12
    m.lathe([(0, r0 * 1.45), (0.18, r0 * 1.05), (0.5, r0), (h - 0.3, r1)], (0, 0, 0), (0, 0, 1),
            "BarkDark", 7, jit=0.01)
    seg = 7
    for i in range(seg):  # jagged splinters
        a0, a1 = 2 * math.pi * i / seg + math.pi / seg, 2 * math.pi * (i + 1) / seg + math.pi / seg
        p0 = (math.cos(a0) * r1, math.sin(a0) * r1, h - 0.3)
        p1 = (math.cos(a1) * r1, math.sin(a1) * r1, h - 0.3)
        tip = (math.cos((a0 + a1) / 2) * r1 * 0.6, math.sin((a0 + a1) / 2) * r1 * 0.6, h - 0.3 + m.r.uniform(0.05, 0.6))
        m._face([m._v(p0), m._v(p1), m._v(tip)], "Wood")
        m._face([m._v(p0), m._v((0, 0, h - 0.25)), m._v(p1)], "WoodLight")
    for a, z, L in ((0.9, 1.9, 0.9), (3.3, 2.2, 0.7), (5.0, 1.5, 0.6)):
        e = (math.cos(a) * L, math.sin(a) * L, z + 0.55)
        m.cyl((0, 0, z), e, 0.06, "BarkDark", seg=5, r1=0.03)
        m.cyl(e, (e[0] * 1.3, e[1] * 1.3 + 0.15, e[2] + 0.45), 0.03, "BarkDark", seg=4, r1=0.01)
    m.collide(-TRUNK_D / 2, TRUNK_D / 2, -TRUNK_D / 2, TRUNK_D / 2, 0, h)
    return m


def tree_stump(name="SM_Tree_Stump"):
    m = Mesh(name)
    m.lathe([(0, 0.28), (0.12, 0.2), (0.4, 0.18)], (0, 0, 0), (0, 0, 1), "Bark", 7, jit=0.01)
    m.cyl((0, 0, 0.4), (0, 0, 0.41), 0.17, "WoodLight", seg=7)
    return m


def bush(name="SM_Bush"):
    m = Mesh(name)
    for (x, y, z, r) in ((0, 0, 0.3, 0.42), (0.25, 0.15, 0.2, 0.3), (-0.22, -0.12, 0.2, 0.32), (0.05, -0.25, 0.15, 0.25)):
        m.ico((x, y, z), r, m.pick("Leaf", "LeafDark", "LeafLight"), sub=1, jit=0.18, flat_bottom=0.0)
    return m


def hedge(name="SM_Hedge"):
    """1 m hedgerow section (decorative / future soft cover)."""
    m = Mesh(name)
    for i in range(4):
        x = -0.375 + i * 0.25
        m.ico((x, m.r.uniform(-0.04, 0.04), 0.55), 0.38, m.pick("Leaf", "LeafDark", "LeafDark"), sub=1,
              scale=(0.9, 0.9, 1.5), jit=0.14, flat_bottom=0.0)
    m.box(-0.5, 0.5, -0.25, 0.25, 0, 0.9, "LeafDark", top_scale=0.85)
    m.clamp_z(1.4)
    m.collide(-0.5, 0.5, -0.3, 0.3, 0, 1.3)
    return m


# =========================================================================== RUBBLE
def _heap(m, n, zmax, spread=0.45, mats=("Plaster", "Brick", "StoneDark", "Rubble", "PlasterDark")):
    # dusty base mound
    m.ico((0, 0, -0.32), 0.62, "Rubble", sub=2, scale=(0.85, 0.85, 0.75), jit=0.08, flat_bottom=0.0)
    for _ in range(n):
        x, y = m.r.uniform(-spread, spread), m.r.uniform(-spread, spread)
        d = math.hypot(x / spread, y / spread)
        z = max(0.02, (1 - d) * zmax * m.r.uniform(0.4, 0.9))
        s = m.r.uniform(0.1, 0.26)
        m.boxc((x, y, z), (s, s * m.r.uniform(0.5, 1.0), s * m.r.uniform(0.4, 0.8)), m.pick(*mats),
               rot=(m.r.uniform(-0.6, 0.6), m.r.uniform(-0.6, 0.6), m.r.uniform(0, 3.14)), jit=0.02)


def rubble_stub(name, seed):
    """Breached wall cell (GDD 9.4): broken stub of the wall and a heap of masonry, all below 0.9 m."""
    m = Mesh(name, seed)
    t = WALL_T / 2
    m.box(-0.5, 0.5, -t, t, 0, 0.30, "StoneDark", jit=0.01)
    n = 6
    h = m.r.uniform(0.45, 0.78)
    for i in range(n):
        x0, x1 = -0.5 + i / n, -0.5 + (i + 1) / n
        h = min(0.8, max(0.3, h + m.r.uniform(-0.2, 0.16)))  # random walk: one continuous broken edge
        if h > 0.32:
            m.box(x0, x1, -0.14, 0.14, 0.3, h, m.pick("Plaster", "PlasterDark"), jit=0.025)
            if m.r.random() < 0.5:  # broken brick core poking out of the plaster
                m.boxc((x0 + (x1 - x0) * m.r.uniform(0.3, 0.7), m.r.uniform(-0.04, 0.04), h + 0.02),
                       (0.12, 0.2, 0.08), "Brick", rot=(m.r.uniform(-0.4, 0.4), m.r.uniform(-0.4, 0.4), m.r.uniform(-0.5, 0.5)),
                       jit=0.015)
    _heap(m, 22, 0.6, mats=("Plaster", "Brick", "BrickDark", "StoneDark", "PlasterDark"))
    if m.r.random() < 0.6:  # fallen timber
        a = m.r.uniform(-0.5, 0.5)
        m.beam((-0.45, -0.35, 0.05), (0.45, 0.25 * math.sin(a) + 0.2, 0.45), 0.09, 0.09, "WoodDark")
    m.clamp_z(RUBBLE_MAX)
    m.collide(-0.5, 0.5, -0.45, 0.45, 0, 0.5)
    return m


def rubble_pile(name, seed):
    m = Mesh(name, seed)
    _heap(m, 20, 0.5)
    m.clamp_z(0.65)
    return m


def rubble_chunk(name, kind):
    m = Mesh(name)
    if kind == "plaster_brick":
        m.box(-0.5, 0.5, -0.5, 0.15, -0.5, 0.5, "Brick", jit=0.06)
        m.box(-0.45, 0.5, 0.15, 0.5, -0.5, 0.45, "Plaster", jit=0.06)
    elif kind == "stone":
        m.ico((0, 0, 0), 0.5, "StoneDark", sub=1, jit=0.25)
    elif kind == "brick":
        m.box(-0.5, 0.5, -0.5, 0.5, -0.5, 0.5, "Brick", jit=0.08)
        m.box(-0.35, 0.3, -0.52, 0.52, 0.48, 0.52, "Rubble", jit=0.04)
    else:  # flat plaster slab
        m._face([m._v(p) for p in ((-0.5, -0.4, -0.5), (0.4, -0.5, -0.5), (0.5, 0.45, -0.5), (-0.3, 0.5, -0.5))], "Plaster")
        m.prism([(-0.5, -0.4), (0.4, -0.5), (0.5, 0.45), (-0.3, 0.5), (-0.55, 0.1)], -0.5, 0.5,
                "PlasterDark", "xy")
    m.normalize()
    return m


# =========================================================================== PROPS / OTHER COVER
def well(name="SM_Well"):
    m = Mesh(name)
    N, r_in, r_out = 10, 0.33, 0.47
    hw = math.tan(math.pi / N) * r_out * 1.04
    for course in range(3):
        z0 = course * 0.27
        for i in range(N):
            a = 2 * math.pi * i / N + (math.pi / N if course % 2 else 0)
            mat = "StoneLight" if course == 2 else m.pick("Stone", "StoneDark", "Stone")
            with m.xf((0, 0, 0), (0, 0, a)):
                m.box(r_in, r_out, -hw, hw, z0 + 0.008, z0 + 0.27, mat, jit=0.008)
    m.cyl((0, 0, 0.42), (0, 0, 0.46), r_in + 0.01, "Water", seg=10)
    for s in (-1, 1):
        m.box(s * 0.40 - 0.045, s * 0.40 + 0.045, -0.045, 0.045, 0.78, 2.12, "WoodDark")
    m.cyl((-0.36, 0, 1.6), (0.36, 0, 1.6), 0.065, "Wood", seg=7)
    m.cyl((0.45, 0, 1.6), (0.53, 0, 1.6), 0.025, "Iron", seg=5)
    m.box(0.52, 0.55, -0.02, 0.02, 1.6, 1.8, "Iron")
    m.cyl((0.53, 0, 1.78), (0.62, 0, 1.78), 0.02, "WoodDark", seg=5)
    m.cyl((0.0, 0, 1.54), (0.0, 0, 1.06), 0.012, "Canvas", seg=4)
    m.lathe([(0, 0.09), (0.2, 0.115)], (0, 0, 0.86), (0, 0, 1), "Wood", seg=8)
    m.cyl((0, 0, 0.86), (0, 0, 0.88), 0.095, "Iron", seg=8)
    # gabled roof along X
    for s in (-1, 1):
        prof = [(0, 2.42), (s * 0.58, 2.06), (s * 0.58, 2.02), (0, 2.37)]
        if s < 0:
            prof = prof[::-1]
        m.prism(prof, -0.58, 0.58, m.pick("Wood", "WoodDark"), "yz")
    m.box(-0.6, 0.6, -0.03, 0.03, 2.38, 2.45, "WoodDark")
    m.box(-0.45, 0.45, -0.04, 0.04, 2.1, 2.38, "WoodDark", top_scale=0.2)
    m.collide(-0.47, 0.47, -0.47, 0.47, 0, 0.81)
    for s in (-1, 1):
        m.collide(s * 0.40 - 0.045, s * 0.40 + 0.045, -0.045, 0.045, 0.81, 2.12)
    return m


def water_trough(name="SM_WaterTrough"):
    """2 x 1 cells (pivot on the shared edge), 0.6 m stone trough."""
    m = Mesh(name)
    L, W, H, t = 0.95, 0.30, 0.6, 0.08
    m.box(-L, L, -W, W, 0, 0.12, "StoneDark")
    m.box(-L, L, -W, -W + t, 0.12, H, "Stone", jit=0.006)
    m.box(-L, L, W - t, W, 0.12, H, "Stone", jit=0.006)
    m.box(-L, -L + t, -W + t, W - t, 0.12, H, "Stone")
    m.box(L - t, L, -W + t, W - t, 0.12, H, "Stone")
    m.box(-L + t, L - t, -W + t, W - t, 0.12, H - 0.1, "Water")
    m.collide(-L, L, -W, W, 0, H)
    return m


def _crate(m, c, s, rz=0.0, mat="Wood", trim="WoodDark"):
    with m.xf(c, (0, 0, rz)):
        h = s / 2
        m.box(-h + 0.015, h - 0.015, -h + 0.015, h - 0.015, 0, s, mat)
        e = 0.05
        for (x, y) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            m.box(x * h - e if x > 0 else -h, x * h if x > 0 else -h + e,
                  y * h - e if y > 0 else -h, y * h if y > 0 else -h + e, 0, s, trim)
        for z in (0, s - e):
            for sx in (-1, 1):
                x0, x1 = sorted((sx * h, sx * (h - e)))
                m.box(x0, x1, -h, h, z, z + e, trim)
                m.box(-h, h, x0, x1, z, z + e, trim)
        # diagonal brace on two faces
        m.beam((h, -h + e, e), (h, h - e, s - e), 0.03, 0.05, trim)
        m.beam((-h + e, h, e), (h - e, h, s - e), 0.03, 0.05, trim)


def crate(name="SM_Crate"):
    m = Mesh(name)
    _crate(m, (0, 0, 0), 0.6)
    m.collide(-0.3, 0.3, -0.3, 0.3, 0, 0.6)
    return m


def crate_stack(name="SM_CrateStack"):
    m = Mesh(name)
    _crate(m, (0, -0.24, 0), 0.45, 0.05)
    _crate(m, (0.02, 0.24, 0), 0.45, -0.04)
    _crate(m, (0.0, 0.0, 0.45), 0.45, 0.25)
    m.collide(-0.46, 0.46, -0.48, 0.48, 0, LOW_H)
    return m


def _barrel(m, c, rz=0.0, lying=False, h=0.88, r=0.29):
    prof = [(0, r * 0.9), (h * 0.5, r), (h, r * 0.9)]
    if lying:
        axis, p0 = (math.cos(rz), math.sin(rz), 0), Vector(c) + Vector((-math.cos(rz) * h / 2, -math.sin(rz) * h / 2, r))
    else:
        axis, p0 = (0, 0, 1), Vector(c)
    m.lathe(prof, p0, axis, "Rust", seg=10)
    ax = Vector(axis).normalized()
    for t in (0.04, h * 0.33, h * 0.67, h - 0.04):
        rr = r * (0.9 + 0.1 * (1 - abs(t - h / 2) / (h / 2))) + 0.006
        m.cyl(p0 + ax * (t - 0.02), p0 + ax * (t + 0.02), rr, "Iron", seg=10)


def barrel(name="SM_Barrel"):
    m = Mesh(name)
    _barrel(m, (0, 0, 0))
    m.collide(-0.29, 0.29, -0.29, 0.29, 0, 0.88)
    return m


def barrel_group(name="SM_BarrelGroup"):
    m = Mesh(name)
    _barrel(m, (-0.18, -0.2, 0))
    _barrel(m, (0.2, -0.12, 0))
    _barrel(m, (-0.05, 0.22, 0), rz=0.3, lying=True)
    m.collide(-0.48, 0.48, -0.48, 0.48, 0, 0.88)
    return m


def woodpile(name="SM_Woodpile"):
    m = Mesh(name)
    rows = [(0.09, 4), (0.09, 4), (0.09, 3), (0.09, 3), (0.09, 2)]
    z = 0.0
    for r, n in rows:
        for i in range(n):
            y = (i - (n - 1) / 2) * 0.19 + m.r.uniform(-0.02, 0.02)
            rr = r * m.r.uniform(0.85, 1.1)
            m.cyl((-0.48 + m.r.uniform(0, 0.04), y, z + rr), (0.48 - m.r.uniform(0, 0.04), y, z + rr), rr,
                  m.pick("Bark", "BarkDark"), seg=6, rot=m.r.uniform(0, 1))
            m.cyl((0.481, y, z + rr), (0.49, y, z + rr), rr * 0.85, "WoodLight", seg=6)
        z += 0.165
    for s in (-1, 1):
        m.box(-0.04, 0.04, s * 0.42 - 0.03, s * 0.42 + 0.03, 0, 0.9, "WoodDark")
    m.collide(-0.5, 0.5, -0.45, 0.45, 0, 0.85)
    return m


def hedgehog(name="SM_CzechHedgehog"):
    """Steel anti-tank obstacle: three angle-iron beams crossed through the centre."""
    m = Mesh(name)
    # three mutually perpendicular beams, turned so the (1,1,1) diagonal points up: it stands on three ends
    R = Vector((1, 1, 1)).normalized().rotation_difference(Vector((0, 0, 1))).to_matrix()
    half = 0.66
    c = Vector((0, 0, half / math.sqrt(3) + 0.02))
    for k, axis in enumerate(((1, 0, 0), (0, 1, 0), (0, 0, 1))):
        d = (R @ Vector(axis)) * half
        with m.xf((0, 0, 0), (0, 0, 0.3)):
            m.beam(c - d, c + d, 0.1, 0.012, "Rust")
            m.beam(c - d, c + d, 0.012, 0.1, "Rust")
    m.collide(-0.45, 0.45, -0.45, 0.45, 0, 0.9)
    return m


def fence(name="SM_Fence_Wood"):
    m = Mesh(name)
    m.box(-0.5, -0.42, -0.04, 0.04, 0, 1.15, "WoodDark", jit=0.008)
    for z in (0.4, 0.85):
        m.boxc((0.0, 0.05, z), (1.0, 0.03, 0.1), "Wood", rot=(0, m.r.uniform(-0.03, 0.03), 0), jit=0.006)
    return m


# =========================================================================== VEHICLES (cover pieces)
def _wheel(m, x, y, z, r, w, hub="VehiclePaint", seg=12):
    m.cyl((x, y - w / 2, z), (x, y + w / 2, z), r, "Rubber", seg=seg)
    s = 1 if y > 0 else -1
    m.cyl((x, y + s * w / 2, z), (x, y + s * (w / 2 + 0.02), z), r * 0.55, hub, seg=8)


def truck(name="SM_Truck_Cargo"):
    """Abandoned 2.5 t cargo truck. Footprint 2 x 6 cells, front +X, ~2.6 m tall (full cover)."""
    m = Mesh(name)
    P, D = "VehiclePaint", "VehicleDark"
    m.box(-2.85, 2.7, -0.45, 0.45, 0.5, 0.72, "Iron")                       # chassis
    m.box(1.6, 2.78, -0.5, 0.5, 0.72, 1.42, P, top_scale=0.97)               # bonnet
    m.box(2.78, 2.85, -0.46, 0.46, 0.74, 1.36, "Iron")                       # grille
    for z in (0.85, 0.98, 1.11, 1.24):
        m.box(2.85, 2.87, -0.42, 0.42, z, z + 0.04, D)
    m.box(2.85, 2.95, -0.98, 0.98, 0.52, 0.68, "Iron")                       # bumper
    for s in (-1, 1):
        y0, y1 = sorted((s * 0.52, s * 0.98))
        m.prism([(1.38, 0.72), (1.55, 1.04), (2.45, 1.04), (2.72, 0.82), (2.8, 0.72)], y0, y1, P, "xz")
        m.cyl((2.45, s * 0.74, 1.16), (2.6, s * 0.74, 1.16), 0.1, D, seg=8)    # headlights
        m.cyl((2.6, s * 0.74, 1.16), (2.61, s * 0.74, 1.16), 0.085, "Glass", seg=8)
        m.box(0.5, 1.42, *sorted((s * 0.76, s * 0.98)), 0.58, 0.63, "Iron")    # running board
        if s < 0:
            m.cyl((0.65, s * 0.6, 0.62), (1.3, s * 0.6, 0.62), 0.2, D, seg=8)  # fuel tank
    # cab
    m.box(0.55, 1.6, -0.95, 0.95, 0.75, 1.45, P)
    m.box(0.55, 1.6, -0.97, 0.97, 2.02, 2.1, P)
    for x in (0.55, 1.52):
        for s in (-1, 1):
            m.box(x, x + 0.08, *sorted((s * 0.88, s * 0.95)), 1.45, 2.02, P)
    m.box(0.62, 1.55, -0.9, 0.9, 1.45, 2.02, "Glass")
    m.box(0.56, 0.62, -0.95, 0.95, 1.45, 2.02, D)                           # back wall
    for s in (-1, 1):  # door lines
        m.box(1.0, 1.02, *sorted((s * 0.95, s * 0.96)), 0.8, 1.42, D)
    # cargo bed with canvas tilt
    m.box(-2.9, 0.48, -1.0, 1.0, 0.92, 1.04, "WoodDark")
    for s in (-1, 1):
        m.box(-2.9, 0.48, *sorted((s * 0.95, s * 1.0)), 1.04, 1.5, P)
        for x in (-2.2, -1.2, -0.2):
            m.box(x, x + 0.06, *sorted((s * 1.0, s * 1.02)), 1.04, 1.5, D)
    m.box(-2.9, -2.85, -0.95, 0.95, 1.04, 1.5, P)
    m.box(0.43, 0.48, -0.95, 0.95, 1.04, 1.5, P)
    arch = [(-1.0, 1.5), (-1.0, 2.2), (-0.78, 2.48), (-0.4, 2.6), (0.4, 2.6), (0.78, 2.48), (1.0, 2.2), (1.0, 1.5)]
    m.prism(arch, -2.86, 0.44, "Canvas", "yz")
    big = [(y * 1.012, z + (0.012 if z > 1.6 else 0)) for y, z in arch]
    for x in (-2.3, -1.25, -0.2):
        m.prism(big, x, x + 0.06, D, "yz")
    # wheels: front axle, rear tandem
    _wheel(m, 2.0, -0.82, 0.45, 0.45, 0.24)
    _wheel(m, 2.0, 0.82, 0.45, 0.45, 0.24)
    for x in (-1.05, -2.15):
        for s in (-1, 1):
            _wheel(m, x, s * 0.76, 0.45, 0.45, 0.36)
    m.collide(-2.95, 0.5, -1.0, 1.0, 0.2, 2.6)
    m.collide(0.5, 1.62, -1.0, 1.0, 0.2, 2.1)
    m.collide(1.62, 2.95, -1.0, 1.0, 0.2, 1.42)
    return m


def car(name="SM_Car_Utility"):
    """Open-top light utility car. Footprint 2 x 4 cells, front +X, ~1.4 m tall."""
    m = Mesh(name)
    P, D = "VehiclePaint", "VehicleDark"
    m.box(-1.7, 1.75, -0.72, 0.72, 0.33, 0.4, D)                            # floor pan
    m.prism([(0.55, 0.4), (1.78, 0.4), (1.78, 0.78), (0.55, 0.95)], -0.62, 0.62, P, "xz")   # bonnet
    m.box(1.78, 1.84, -0.5, 0.5, 0.42, 0.76, "Iron")                        # grille
    m.box(1.84, 1.92, -0.8, 0.8, 0.36, 0.48, "Iron")                        # bumper
    m.box(-1.75, -1.0, -0.72, 0.72, 0.4, 0.92, P)                           # rear deck
    for s in (-1, 1):
        m.box(-1.0, 0.55, *sorted((s * 0.66, s * 0.72)), 0.4, 0.92, P)      # tub sides
        m.box(-0.2, -0.18, *sorted((s * 0.72, s * 0.73)), 0.45, 0.88, D)
        for x in (-1.2, 1.2):
            y0, y1 = sorted((s * 0.6, s * 0.82))
            m.prism([(x - 0.48, 0.5), (x - 0.32, 0.74), (x + 0.32, 0.74), (x + 0.48, 0.5)], y0, y1, P, "xz")
            _wheel(m, x, s * 0.7, 0.33, 0.33, 0.2, seg=10)
        m.cyl((1.66, s * 0.45, 0.82), (1.76, s * 0.45, 0.82), 0.075, D, seg=8)
    # windscreen frame (upright) with glass
    for s in (-1, 1):
        m.box(0.52, 0.56, *sorted((s * 0.6, s * 0.66)), 0.95, 1.38, D)
    m.box(0.52, 0.56, -0.66, 0.66, 1.34, 1.38, D)
    m.box(0.53, 0.55, -0.6, 0.6, 0.95, 1.34, "Glass")
    # seats
    for s in (-1, 1):
        m.box(-0.1, 0.3, s * 0.3 - 0.2, s * 0.3 + 0.2, 0.4, 0.62, "Canvas")
        m.box(-0.14, -0.06, s * 0.3 - 0.2, s * 0.3 + 0.2, 0.62, 1.0, "Canvas")
    m.box(-0.95, -0.55, -0.6, 0.6, 0.4, 0.62, "Canvas")
    m.box(-1.0, -0.92, -0.6, 0.6, 0.62, 1.0, "Canvas")
    m.cyl((0.45, 0.3, 0.75), (0.3, 0.3, 0.95), 0.02, "Iron", seg=5)
    m.lathe([(0, 0.17), (0.02, 0.17)], (0.3, 0.3, 0.95), (0.6, 0, 1), "Iron", seg=10)
    # spare wheel on the tail, jerrycan
    m.cyl((-1.75, 0, 0.7), (-1.93, 0, 0.7), 0.31, "Rubber", seg=10)
    m.box(-1.6, -1.3, 0.45, 0.6, 0.92, 1.2, D)
    m.collide(-1.95, 1.92, -0.82, 0.82, 0.12, 0.95)
    return m


def tank_wreck(name="SM_Tank_Wreck"):
    """Knocked-out generic medium tank hulk. Footprint 3 x 6 cells, front +X, ~2.4 m tall."""
    m = Mesh(name)
    P, D = "VehiclePaint", "VehicleDark"
    track = [(-2.95, 0.38), (-2.78, 0.1), (-2.35, 0.0), (2.35, 0.0), (2.8, 0.1), (2.98, 0.55), (2.82, 0.86),
             (-2.82, 0.86), (-2.98, 0.62)]
    for s in (-1, 1):
        y0, y1 = sorted((s * 0.98, s * 1.45))
        m.prism(track, y0, y1, "Iron", "xz")
        for i in range(5):
            x = -2.1 + i * 1.05
            yy0, yy1 = sorted((s * 1.45, s * 1.49))
            m.cyl((x, yy0, 0.38), (x, yy1, 0.38), 0.31, D, seg=10)
        m.cyl((2.62, s * 1.45, 0.62), (2.62, s * 1.5, 0.62), 0.2, "Rust", seg=8)
    m.box(-2.9, 2.8, -0.98, 0.98, 0.3, 1.0, D)
    hull = [(-2.98, 0.95), (2.88, 0.95), (2.98, 1.05), (2.25, 1.72), (-2.72, 1.72), (-2.98, 1.45)]
    m.prism(hull, -1.42, 1.42, P, "xz")
    # missing track section on the left: a few links lying on the ground
    for i in range(4):
        m.boxc((1.2 + i * 0.32, -1.25 + m.r.uniform(-0.05, 0.05), 0.04), (0.28, 0.42, 0.08), "Iron",
               rz=m.r.uniform(-0.25, 0.25), jit=0.01)
    # engine deck grilles, hatches
    for x in (-2.4, -1.9):
        m.box(x, x + 0.4, -0.8, 0.8, 1.72, 1.75, "Iron")
    m.box(1.55, 1.95, *sorted((0.35, 0.8)), 1.68, 1.76, D)
    # turret (traversed and slumped)
    m.box(-0.9, 0.7, -0.9, 0.9, 1.72, 1.78, D)                              # turret ring
    with m.xf((-0.1, 0.0, 1.78), (0.0, 0.03, math.radians(28))):
        m.lathe([(0, 0.92), (0.55, 0.86), (0.68, 0.7), (0.7, 0)], (0, 0, 0), (0, 0, 1), P, seg=8, rot=0.2)
        m.box(-1.2, -0.75, -0.55, 0.55, 0.08, 0.55, P)                     # turret bustle
        m.box(0.8, 1.12, -0.4, 0.4, 0.08, 0.6, D)                          # mantlet
        m.cyl((1.1, 0, 0.34), (3.1, 0, 0.16), 0.085, P, seg=8)            # drooping gun
        m.cyl((3.1, 0, 0.16), (3.3, 0, 0.15), 0.11, D, seg=8)
        m.cyl((-0.35, 0.38, 0.66), (-0.35, 0.38, 0.9), 0.24, D, seg=8)    # cupola, hatch blown open
        m.boxc((-0.35, 0.72, 1.08), (0.42, 0.05, 0.42), D, rot=(math.radians(-70), 0, 0))
    # scorch marks
    for s in (-1, 1):
        y = s * 1.425
        m.box(-0.9, 0.3, *sorted((y, y + s * 0.005)), 1.05, 1.6, "Soot", jit=0.01)
    m.collide(-3.0, 3.0, -1.5, 1.5, 0, 1.72)
    m.collide(-1.25, 0.75, -1.0, 1.0, 1.72, 2.35)
    return m


def hay_cart(name="SM_Cart_Hay"):
    """Two-wheeled farm cart loaded with hay. Footprint 2 x 3 cells, shafts +X."""
    m = Mesh(name)
    ox = -0.47
    with m.xf((ox, 0, 0)):
        m.box(-1.0, 1.0, -0.62, 0.62, 0.68, 0.76, "Wood")
        for s in (-1, 1):
            m.box(-1.0, 1.0, *sorted((s * 0.6, s * 0.66)), 0.76, 1.0, "WoodPaint")
            for x in (-0.95, -0.3, 0.35, 0.9):
                m.box(x, x + 0.06, *sorted((s * 0.6, s * 0.68)), 0.62, 1.05, "WoodDark")
            # spoked wheel
            yw = s * 0.79
            N = 12
            pts = [Vector((math.cos(2 * math.pi * i / N) * 0.58, yw, 0.6 + math.sin(2 * math.pi * i / N) * 0.58)) for i in range(N)]
            for i in range(N):
                m.beam(pts[i], pts[(i + 1) % N], 0.07, 0.07, "WoodDark")
            for i in range(0, N, 2):
                m.beam(Vector((0, yw, 0.6)), pts[i], 0.035, 0.035, "Wood")
            m.cyl((0, yw - 0.06, 0.6), (0, yw + 0.06, 0.6), 0.09, "Iron", seg=8)
            m.cyl((0.9, s * 0.45, 0.7), (1.95, s * 0.38, 0.1), 0.04, "Wood", seg=5)   # shafts
        m.cyl((0, -0.85, 0.6), (0, 0.85, 0.6), 0.04, "Iron", seg=6)
        m.beam(Vector((-0.9, 0, 0.68)), Vector((-0.9, 0, 0.0)), 0.06, 0.06, "WoodDark")  # prop
        # hay load
        m.box(-0.95, 0.95, -0.58, 0.58, 0.76, 1.3, "Hay", jit=0.04, top_scale=0.9)
        for _ in range(7):
            m.ico((m.r.uniform(-0.7, 0.7), m.r.uniform(-0.35, 0.35), 1.3), 0.32, m.pick("Hay", "HayDark"),
                  sub=1, scale=(1.2, 1, 0.6), jit=0.15)
    m.collide(-1.47, 0.53, -0.88, 0.88, 0, 1.45)
    return m


# =========================================================================== INTERIOR (face +X)
def table(name="SM_Int_Table"):
    m = Mesh(name)
    m.box(-0.42, 0.42, -0.72, 0.72, 0.72, 0.76, "Wood")
    for x in (-0.36, 0.31):
        for y in (-0.66, 0.61):
            m.box(x, x + 0.05, y, y + 0.05, 0, 0.72, "WoodDark", top_scale=1.0)
    m.box(-0.34, 0.34, -0.64, -0.62, 0.62, 0.72, "WoodDark")
    m.box(-0.34, 0.34, 0.62, 0.64, 0.62, 0.72, "WoodDark")
    # a few things on the table
    m.cyl((0.1, -0.3, 0.76), (0.1, -0.3, 0.775), 0.11, "Fabric", seg=10)
    m.lathe([(0, 0.035), (0.12, 0.04), (0.14, 0.02), (0.2, 0.015)], (-0.15, 0.25, 0.76), (0, 0, 1), "Glass", seg=6)
    m.box(-0.1, 0.12, 0.3, 0.45, 0.76, 0.79, "Fabric")
    m.collide(-0.42, 0.42, -0.72, 0.72, 0, 0.76)
    return m


def chair(name="SM_Int_Chair"):
    m = Mesh(name)
    m.box(-0.21, 0.21, -0.21, 0.21, 0.43, 0.47, "Wood")
    for x in (-0.2, 0.16):
        for y in (-0.2, 0.16):
            m.box(x, x + 0.04, y, y + 0.04, 0, 0.43, "WoodDark")
    for y in (-0.2, 0.16):
        m.box(-0.21, -0.17, y, y + 0.04, 0.47, 0.95, "WoodDark")
    for z in (0.62, 0.84):
        m.box(-0.21, -0.18, -0.17, 0.17, z, z + 0.08, "Wood")
    m.collide(-0.21, 0.21, -0.21, 0.21, 0, 0.95)
    return m


def bench(name="SM_Int_Bench"):
    m = Mesh(name)
    m.box(-0.16, 0.16, -0.7, 0.7, 0.42, 0.46, "Wood")
    for s in (-1, 1):
        m.box(-0.13, 0.13, s * 0.58 - 0.025, s * 0.58 + 0.025, 0, 0.42, "WoodDark")
    m.box(-0.03, 0.03, -0.56, 0.56, 0.12, 0.18, "WoodDark")
    m.collide(-0.16, 0.16, -0.7, 0.7, 0, 0.46)
    return m


def bed(name="SM_Int_Bed"):
    m = Mesh(name)
    m.box(-1.0, -0.95, -0.5, 0.5, 0, 1.0, "WoodDark", top_scale=1.0)
    m.box(-1.0, -0.95, -0.42, 0.42, 1.0, 1.06, "WoodDark")
    m.box(0.95, 1.0, -0.5, 0.5, 0, 0.7, "WoodDark")
    for s in (-1, 1):
        m.box(-0.95, 0.95, *sorted((s * 0.46, s * 0.5)), 0.22, 0.36, "Wood")
    m.box(-0.95, 0.95, -0.46, 0.46, 0.3, 0.52, "Fabric")
    m.box(-0.92, -0.6, -0.33, 0.33, 0.52, 0.64, "Fabric", jit=0.015)
    m.box(-0.5, 0.97, -0.5, 0.5, 0.5, 0.56, "FabricRed")
    for s in (-1, 1):
        m.box(-0.5, 0.97, *sorted((s * 0.5, s * 0.52)), 0.28, 0.56, "FabricRed")
    m.box(-0.5, -0.38, -0.5, 0.5, 0.56, 0.585, "Fabric")
    m.collide(-1.0, 1.0, -0.52, 0.52, 0, 0.64)
    return m


def wardrobe(name="SM_Int_Wardrobe"):
    m = Mesh(name)
    m.box(-0.28, 0.28, -0.5, 0.5, 0, 0.1, "WoodDark")
    m.box(-0.28, 0.27, -0.5, 0.5, 0.1, 1.8, "Wood")
    m.box(-0.31, 0.31, -0.53, 0.53, 1.8, 1.92, "WoodDark")
    for y0, y1 in ((-0.47, -0.01), (0.01, 0.47)):
        m.box(0.27, 0.29, y0, y1, 0.15, 1.75, "WoodLight")
        m.box(0.29, 0.30, y0 + 0.05, y1 - 0.05, 0.25, 1.65, "Wood")
    for y in (-0.05, 0.03):
        m.box(0.29, 0.32, y, y + 0.02, 0.95, 1.05, "Brass")
    m.collide(-0.31, 0.31, -0.53, 0.53, 0, 1.92)
    return m


def dresser(name="SM_Int_Dresser"):
    m = Mesh(name)
    m.box(-0.25, 0.24, -0.5, 0.5, 0.06, 0.82, "Wood")
    m.box(-0.27, 0.27, -0.52, 0.52, 0.82, 0.86, "WoodDark")
    for x in (-0.24, 0.19):
        for y in (-0.49, 0.45):
            m.box(x, x + 0.05, y, y + 0.04, 0, 0.06, "WoodDark")
    for i in range(3):
        z0 = 0.1 + i * 0.235
        m.box(0.24, 0.26, -0.46, 0.46, z0, z0 + 0.21, "WoodLight")
        for y in (-0.22, 0.2):
            m.box(0.26, 0.29, y, y + 0.03, z0 + 0.09, z0 + 0.12, "Brass")
    # mirror on top and a jug
    m.box(-0.24, -0.2, -0.3, 0.3, 0.86, 1.55, "WoodDark")
    m.box(-0.2, -0.19, -0.26, 0.26, 0.9, 1.51, "Glass")
    m.lathe([(0, 0.07), (0.12, 0.09), (0.22, 0.05), (0.26, 0.06)], (0.05, 0.3, 0.86), (0, 0, 1), "Fabric", seg=8)
    m.collide(-0.27, 0.27, -0.52, 0.52, 0, 0.86)
    return m


def bookshelf(name="SM_Int_Bookshelf"):
    m = Mesh(name)
    for s in (-1, 1):
        m.box(-0.16, 0.16, *sorted((s * 0.45, s * 0.42)), 0, 1.8, "WoodDark")
    m.box(-0.16, -0.14, -0.42, 0.42, 0, 1.8, "Wood")
    for z in (0.0, 0.45, 0.9, 1.35, 1.77):
        m.box(-0.14, 0.16, -0.42, 0.42, z, z + 0.03, "WoodDark")
    for z in (0.03, 0.48, 0.93, 1.38):
        y = -0.41
        while y < 0.38:
            if m.r.random() < 0.12:
                m.lathe([(0, 0.05), (0.14, 0.05), (0.18, 0.03)], (0.0, y + 0.05, z), (0, 0, 1),
                        m.pick("Glass", "Fabric"), seg=6)
                y += 0.12
                continue
            w = m.r.uniform(0.03, 0.06)
            h = m.r.uniform(0.2, 0.3)
            if y + w > 0.41:
                break
            lean = m.r.uniform(-0.05, 0.05) if m.r.random() < 0.2 else 0.0
            m.boxc((0.0, y + w / 2, z + h / 2), (0.2, w, h),
                   m.pick("FabricRed", "FabricBlue", "FabricGreen", "WoodDark", "Canvas"), rot=(lean, 0, 0))
            y += w + 0.004
    m.collide(-0.16, 0.16, -0.45, 0.45, 0, 1.8)
    return m


def stove(name="SM_Int_Stove"):
    m = Mesh(name)
    for x in (-0.2, 0.17):
        for y in (-0.25, 0.22):
            m.box(x, x + 0.04, y, y + 0.04, 0, 0.15, "Iron", top_scale=0.7)
    m.box(-0.25, 0.25, -0.3, 0.3, 0.15, 0.75, "Iron")
    m.box(-0.27, 0.27, -0.32, 0.32, 0.75, 0.79, "Iron")
    m.box(0.25, 0.27, -0.16, 0.16, 0.3, 0.58, "Steel")
    m.box(0.27, 0.3, 0.08, 0.13, 0.42, 0.46, "Brass")
    for y in (-0.15, 0.15):
        m.cyl((0.08, y, 0.79), (0.08, y, 0.8), 0.1, "Steel", seg=8)
    m.cyl((-0.12, 0, 0.79), (-0.12, 0, 2.6), 0.065, "Iron", seg=8)
    m.cyl((-0.12, 0, 1.4), (-0.12, 0, 1.44), 0.08, "Iron", seg=8)
    m.collide(-0.27, 0.27, -0.32, 0.32, 0, 0.8)
    return m


def fireplace(name="SM_Int_Fireplace"):
    """Stands against a wall at its back (-X), chimney breast to the 2.6 m ceiling line."""
    m = Mesh(name)
    m.box(-0.3, 0.28, -0.78, 0.78, 0, 0.08, "StoneDark")
    for s in (-1, 1):
        m.box(-0.3, 0.1, *sorted((s * 0.45, s * 0.72)), 0.08, 1.1, "Stone", jit=0.005)
    m.box(-0.3, -0.18, -0.45, 0.45, 0.08, 1.1, "Soot")
    m.box(-0.3, 0.18, -0.78, 0.78, 1.1, 1.2, "WoodDark")
    m.box(-0.3, 0.06, -0.66, 0.66, 1.2, WALL_H, "Plaster")
    for i in range(3):
        y = -0.18 + i * 0.18
        m.cyl((-0.15 + 0.03 * i, y - 0.15, 0.14 + 0.06 * (i % 2)), (0.0 + 0.02 * i, y + 0.15, 0.14), 0.05,
              m.pick("Bark", "BarkDark"), seg=6)
    # bits on the mantel
    m.box(-0.15, -0.05, -0.5, -0.35, 1.2, 1.42, "WoodDark")
    m.lathe([(0, 0.05), (0.15, 0.04), (0.2, 0.0)], (-0.1, 0.4, 1.2), (0, 0, 1), "Brass", seg=6)
    m.collide(-0.3, 0.28, -0.78, 0.78, 0, 1.2)
    m.collide(-0.3, 0.06, -0.66, 0.66, 1.2, WALL_H)
    return m


def rug(name="SM_Int_Rug"):
    m = Mesh(name)
    m.box(-0.7, 0.7, -1.0, 1.0, 0, 0.008, "FabricRed")
    m.box(-0.6, 0.6, -0.9, 0.9, 0.008, 0.011, "Canvas")
    m.box(-0.5, 0.5, -0.8, 0.8, 0.011, 0.014, "FabricRed")
    m._face([m._v(p) for p in ((0, -0.5, 0.0145), (0.3, 0, 0.0145), (0, 0.5, 0.0145), (-0.3, 0, 0.0145))], "FabricBlue")
    return m


def lamp(name="SM_Int_OilLamp"):
    m = Mesh(name)
    m.lathe([(0, 0.07), (0.02, 0.07), (0.05, 0.03), (0.1, 0.06), (0.17, 0.065), (0.2, 0.03)], (0, 0, 0),
            (0, 0, 1), "Brass", seg=8)
    m.lathe([(0, 0.03), (0.06, 0.045), (0.12, 0.025), (0.2, 0.025)], (0, 0, 0.2), (0, 0, 1), "Glass", seg=8)
    return m


def bucket(name="SM_Int_Bucket"):
    m = Mesh(name)
    m.lathe([(0, 0.11), (0.28, 0.14)], (0, 0, 0), (0, 0, 1), "Wood", seg=9)
    for z in (0.04, 0.22):
        m.cyl((0, 0, z), (0, 0, z + 0.025), 0.118 + z * 0.1, "Iron", seg=9)
    m.cyl((0, 0, 0.27), (0, 0, 0.28), 0.12, "Water", seg=9)
    return m


def sacks(name="SM_Int_Sacks"):
    m = Mesh(name)
    m.bag((-0.1, -0.15, 0), 0.75, 0.42, 0.22, "Sandbag", rz=0.1, jit=0.02)
    m.bag((0.05, 0.25, 0), 0.75, 0.42, 0.22, "Sandbag", rz=-0.15, jit=0.02)
    m.bag((-0.02, 0.05, 0.21), 0.75, 0.42, 0.22, "Canvas", rz=0.4, jit=0.02)
    m.collide(-0.45, 0.45, -0.45, 0.45, 0, 0.42)
    return m


def radio(name="SM_Int_Radio"):
    m = Mesh(name)
    m.box(-0.12, 0.12, -0.22, 0.22, 0, 0.26, "WoodDark")
    m.prism([(-0.12, 0.26), (0.12, 0.26), (0.08, 0.33), (-0.08, 0.33)], -0.2, 0.2, "WoodDark", "xz")
    m.box(0.12, 0.125, -0.18, 0.04, 0.05, 0.24, "Canvas")
    m.cyl((0.12, 0.12, 0.14), (0.14, 0.12, 0.14), 0.05, "Brass", seg=8)
    for y in (0.09, 0.16):
        m.cyl((0.12, y, 0.05), (0.15, y, 0.05), 0.018, "Iron", seg=6)
    return m


def piano(name="SM_Int_Piano"):
    m = Mesh(name)
    m.box(-0.3, 0.05, -0.75, 0.75, 0, 1.25, "WoodDark")
    m.box(-0.32, 0.08, -0.77, 0.77, 1.25, 1.29, "WoodDark")
    m.box(0.05, 0.32, -0.74, 0.74, 0.68, 0.76, "WoodDark")
    m.box(0.05, 0.27, -0.66, 0.66, 0.76, 0.785, "Fabric")
    for i in range(36):
        if i % 7 in (2, 6):
            continue
        y = -0.64 + i * 0.036
        m.box(0.05, 0.17, y, y + 0.016, 0.785, 0.8, "Soot")
    m.box(0.05, 0.12, -0.74, 0.74, 0.8, 0.88, "WoodDark")
    m.box(0.05, 0.08, -0.4, 0.4, 0.88, 1.1, "Wood")
    for s in (-1, 1):
        m.box(0.26, 0.32, *sorted((s * 0.68, s * 0.74)), 0, 0.68, "WoodDark")
        m.cyl((0.0, s * 0.55, 1.29), (0.0, s * 0.55, 1.33), 0.05, "Brass", seg=6)
    m.collide(-0.32, 0.32, -0.77, 0.77, 0, 1.29)
    return m


# =========================================================================== build
CATEGORIES = [
    ("Walls", [
        wall_plaster, lambda: wall_plaster("SM_Wall_Plaster_Patched", patched=True), wall_brick,
        wall_weakened, lambda: wall_weakened("SM_Wall_Plaster_Weakened_B"),
        lambda: wall_pillar("SM_Wall_Pillar_Plaster", "Plaster"),
        lambda: wall_pillar("SM_Wall_Pillar_Brick", "Brick"),
        wall_doorway,
        lambda: wall_window("SM_Wall_Window_Plaster", "Plaster"),
        lambda: wall_window("SM_Wall_Window_Brick", "Brick"),
        lambda: wall_window("SM_Wall_Window_Plaster_Broken", "Plaster", broken=True),
        wall_wood, lambda: wall_wood("SM_Wall_Wood_B"), wall_wood_window,
    ]),
    ("LowCover", [
        lowwall_stone, lambda: lowwall_stone("SM_LowWall_Stone_Damaged", damaged=True), lowwall_brick,
        sandbags, hay_stack, lambda: hay_stack("SM_HayBale_Stack_B", 1), hay_single, hay_loose,
        crate_stack, barrel_group, woodpile,
    ]),
    ("Trees", [tree_oak, tree_apple, tree_poplar, tree_pine, tree_dead, tree_stump, bush, hedge]),
    ("Rubble", [
        lambda: rubble_stub("SM_Rubble_WallStub_A", 11), lambda: rubble_stub("SM_Rubble_WallStub_B", 23),
        lambda: rubble_stub("SM_Rubble_WallStub_C", 37),
        lambda: rubble_pile("SM_Rubble_Pile_A", 5), lambda: rubble_pile("SM_Rubble_Pile_B", 8),
        lambda: rubble_chunk("SM_Rubble_Chunk_A", "plaster_brick"), lambda: rubble_chunk("SM_Rubble_Chunk_B", "stone"),
        lambda: rubble_chunk("SM_Rubble_Chunk_C", "brick"), lambda: rubble_chunk("SM_Rubble_Chunk_D", "slab"),
    ]),
    ("Props", [well, water_trough, crate, barrel, hedgehog, fence]),
    ("Vehicles", [truck, car, tank_wreck, hay_cart]),
    ("Interior", [table, chair, bench, bed, wardrobe, dresser, bookshelf, stove, fireplace, rug, piano,
                  radio, lamp, bucket, sacks]),
]


def build():
    bpy.ops.wm.read_homefile(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    make_materials()
    root = bpy.data.collections.new("Cover")
    scene.collection.children.link(root)
    col_coll = bpy.data.collections.new("Collision")
    scene.collection.children.link(col_coll)
    O = {}
    for cat, makers in CATEGORIES:
        c = bpy.data.collections.new(cat)
        root.children.link(c)
        O[cat] = [mk().finish(c, col_coll) for mk in makers]
    layout(O)
    return O


def _bounds(ob):
    cs = [v.co for v in ob.data.vertices]
    return (Vector((min(c.x for c in cs), min(c.y for c in cs), min(c.z for c in cs))),
            Vector((max(c.x for c in cs), max(c.y for c in cs), max(c.z for c in cs))))


def _collision_of(ob):
    return [o for o in bpy.data.objects if o.get("collision_for") == ob.name]


def layout(O, gap=1.0, row_gap=2.5):
    """One row per category along X; rows stacked along -Y. Collision follows its mesh."""
    y = 0.0
    for cat, obs in O.items():
        x, depth = 0.0, 0.0
        for ob in obs:
            mn, mx = _bounds(ob)
            ob.location = (x - mn.x, y - mx.y, 0)
            for c in _collision_of(ob):
                c.location = ob.location
            x += (mx.x - mn.x) + gap
            depth = max(depth, mx.y - mn.y)
        y -= depth + row_gap


# --------------------------------------------------------------------------- preview
def _grid_floor(prev, mn, mx):
    """Ground plane with 1 m grid lines, to show footprints against grid cells."""
    bm = bmesh.new()
    x0, x1, y0, y1 = math.floor(mn.x) - 1, math.ceil(mx.x) + 1, math.floor(mn.y) - 1, math.ceil(mx.y) + 1
    for p in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        bm.verts.new((p[0], p[1], -0.002))
    bm.faces.new(bm.verts)
    me = bpy.data.meshes.new("PreviewFloor")
    bm.to_mesh(me); bm.free()
    floor = bpy.data.objects.new("PreviewFloor", me)
    mat = bpy.data.materials.new("M_PreviewFloor")
    mat.diffuse_color = (0.30, 0.31, 0.28, 1)
    me.materials.append(mat)
    prev.objects.link(floor)
    bm = bmesh.new()
    w = 0.012
    for x in range(x0, x1 + 1):
        v = [bm.verts.new(p) for p in ((x - w, y0, 0), (x + w, y0, 0), (x + w, y1, 0), (x - w, y1, 0))]
        bm.faces.new(v)
    for y in range(y0, y1 + 1):
        v = [bm.verts.new(p) for p in ((x0, y - w, 0.0005), (x1, y - w, 0.0005), (x1, y + w, 0.0005), (x0, y + w, 0.0005))]
        bm.faces.new(v)
    me = bpy.data.meshes.new("PreviewGrid")
    bm.to_mesh(me); bm.free()
    grid = bpy.data.objects.new("PreviewGrid", me)
    gmat = bpy.data.materials.new("M_PreviewGrid")
    gmat.diffuse_color = (0.22, 0.23, 0.21, 1)
    me.materials.append(gmat)
    prev.objects.link(grid)


def setup_preview(scene):
    bpy.context.view_layer.update()
    objs = [o for o in bpy.data.collections["Cover"].all_objects if o.type == "MESH"]
    pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    mn = Vector((min(p.x for p in pts), min(p.y for p in pts), 0))
    mx = Vector((max(p.x for p in pts), max(p.y for p in pts), 0))
    prev = bpy.data.collections.get("Preview") or bpy.data.collections.new("Preview")
    if prev.name not in scene.collection.children:
        scene.collection.children.link(prev)
    _grid_floor(prev, mn, mx)
    cam_data = bpy.data.cameras.new("PreviewCam")
    cam_data.type = "ORTHO"
    cam = bpy.data.objects.new("PreviewCam", cam_data)
    prev.objects.link(cam)
    cam.rotation_euler = (math.radians(62), 0, math.radians(14))
    scene.camera = cam
    scene.render.resolution_x, scene.render.resolution_y = 1920, 1080
    if scene.world is None:
        scene.world = bpy.data.worlds.new("World")
    scene.world.color = (0.16, 0.18, 0.2)
    scene.render.engine = "BLENDER_WORKBENCH"
    sh = scene.display.shading
    sh.light = "STUDIO"
    sh.color_type = "MATERIAL"
    sh.show_cavity = True
    sh.show_shadows = True
    sh.show_object_outline = False
    return cam


def frame(cam, objs, aspect=16 / 9, margin=1.08):
    bpy.context.view_layer.update()
    inv = cam.matrix_world.inverted()
    pts = [inv @ (o.matrix_world @ Vector(c)) for o in objs for c in o.bound_box]
    xs, ys = [p.x for p in pts], [p.y for p in pts]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    w, h = max(xs) - min(xs), max(ys) - min(ys)
    cam.data.ortho_scale = max(w, h * aspect) * margin
    rot = cam.matrix_world.to_3x3()
    centre_world = cam.matrix_world @ Vector((cx, cy, 0))
    cam.location = centre_world - rot @ Vector((0, 0, -60))  # back off along the view axis


def render_previews():
    scene = bpy.context.scene
    cam = setup_preview(scene)
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    cover = bpy.data.collections["Cover"]
    # chunks are unit cubes for ABreachableWall to scale; show them at a typical debris size
    chunks = [o for o in cover.all_objects if "_Chunk_" in o.name]
    for o in chunks:
        o.scale = (0.3, 0.3, 0.3)
        o.location.z = 0.15
    out = []
    for cat in [c.name for c in cover.children] + ["All"]:
        objs = []
        for c in cover.children:
            show = cat in ("All", c.name)
            for o in c.objects:
                o.hide_render = not show
                if show:
                    objs.append(o)
        frame(cam, objs)
        scene.render.filepath = os.path.join(PREVIEW_DIR, "Cover_%s.png" % cat)
        bpy.ops.render.render(write_still=True)
        out.append(scene.render.filepath)
    for c in cover.children:
        for o in c.objects:
            o.hide_render = False
    for o in chunks:
        o.scale = (1, 1, 1)
        o.location.z = 0.0
    return out


# --------------------------------------------------------------------------- export
def export_fbx(folder=FBX_DIR, only=None):
    os.makedirs(folder, exist_ok=True)
    vl = bpy.context.view_layer
    out = []
    for ob in [o for o in bpy.data.collections["Cover"].all_objects if o.type == "MESH"]:
        if only and ob.name not in only:
            continue
        cols = _collision_of(ob)
        sel = [ob] + cols
        saved = {o: o.location.copy() for o in sel}
        for o in sel:
            o.location = (0, 0, 0)
        vl.update()
        for o in vl.objects:
            o.select_set(False)
        for o in sel:
            o.select_set(True)
        vl.objects.active = ob
        path = os.path.join(folder, ob.name + ".fbx")
        with bpy.context.temp_override(selected_objects=sel, active_object=ob, object=ob):
            bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"},
                                     mesh_smooth_type="FACE", apply_unit_scale=True,
                                     bake_space_transform=False, add_leaf_bones=False,
                                     use_mesh_modifiers=True, axis_forward="-Z", axis_up="Y")
        for o, loc in saved.items():
            o.location = loc
        out.append(path)
    vl.update()
    write_manifest(folder)
    return out


def write_manifest(folder=FBX_DIR):
    """manifest.json for the Unreal import script: category per mesh and linear material colours."""
    import json
    cover = bpy.data.collections["Cover"]
    data = {
        "meshes": {o.name: c.name for c in cover.children for o in c.objects if o.type == "MESH"},
        "materials": {"M_Env_" + k: {"color": [_lin(c) for c in v], "metallic": METALLIC.get(k, 0.0),
                                     "roughness": ROUGHNESS.get(k, 0.85)} for k, v in PALETTE.items()},
    }
    with open(os.path.join(folder, "manifest.json"), "w") as f:
        json.dump(data, f, indent=1)


def report(O):
    lines = []
    for cat, obs in O.items():
        for ob in obs:
            tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
            mn, mx = _bounds(ob)
            d = mx - mn
            lines.append("%-32s %-9s tris %5d  size %.2f x %.2f x %.2f  ucx %d" %
                         (ob.name, cat, tris, d.x, d.y, d.z, len(_collision_of(ob))))
    return "\n".join(lines)


if __name__ == "__main__" or True:
    objs = build()
    bpy.ops.wm.save_as_mainfile(filepath=BLEND_PATH)
    print(report(objs))
    if os.environ.get("COVER_EXPORT", "1") == "1":
        print("Exported %d FBX" % len(export_fbx()))
    if os.environ.get("COVER_PREVIEW", "1") == "1":
        print("\n".join(render_previews()))
    bpy.ops.wm.save_as_mainfile(filepath=BLEND_PATH)
