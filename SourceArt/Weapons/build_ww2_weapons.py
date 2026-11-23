"""
Squad Command - procedural low-poly WW2 weapon set.

Run inside Blender (tested on 5.3):
    exec(open(r"<this file>").read(), {"__file__": r"<this file>"})

Builds every weapon as ONE mesh object with flat-shaded faces and shared flat-colour
materials, lays them out for an overview render, saves WW2_Weapons_LowPoly.blend next
to this script and exports one FBX per weapon to ./FBX for Unreal.

Conventions (match Unreal):
  * 1 Blender unit = 1 m (Unreal imports as 100 uu).
  * Muzzle points +X (imports as Unreal +X / forward), up is +Z.
  * Object origin = firing-hand grip (trigger) for hand-held weapons, ground contact for
    the M2 tripod / mortar / flamethrower pack, centre for grenades, scopes and ammo.
  * Child empties named SOCKET_<Name> import as Static Mesh sockets
    (SOCKET_Muzzle on every firearm, SOCKET_Backblast on the AT launchers).
"""
import bpy, bmesh, math, os
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
BLEND_PATH = os.path.join(HERE, "WW2_Weapons_LowPoly.blend")
FBX_DIR = os.path.join(HERE, "FBX")
PREVIEW_PATH = os.path.join(HERE, "WW2_Weapons_Preview.png")

# --------------------------------------------------------------------------- materials
PALETTE = {  # sRGB
    "Wood":      (0.62, 0.40, 0.22),
    "WoodDark":  (0.40, 0.24, 0.13),
    "Metal":     (0.20, 0.20, 0.21),
    "MetalLight":(0.42, 0.42, 0.44),
    "Steel":     (0.66, 0.67, 0.69),
    "Olive":     (0.42, 0.47, 0.33),
    "Tan":       (0.80, 0.68, 0.48),
    "Brass":     (0.82, 0.63, 0.30),
    "Copper":    (0.74, 0.44, 0.27),
    "Bakelite":  (0.11, 0.10, 0.09),
}
METALLIC = {"Metal": 0.5, "MetalLight": 0.5, "Steel": 0.8, "Brass": 0.7, "Copper": 0.7}
MATS = {}


def _lin(c):
    return ((c + 0.055) / 1.055) ** 2.4 if c > 0.04045 else c / 12.92


def make_materials():
    for key, srgb in PALETTE.items():
        name = "M_Weapon_" + key
        mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
        lin = tuple(_lin(c) for c in srgb) + (1.0,)
        mat.diffuse_color = lin
        mat.roughness = 0.7
        mat.metallic = METALLIC.get(key, 0.0)
        if mat.node_tree is None and hasattr(mat, "use_nodes"):
            mat.use_nodes = True
        bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if bsdf:
            bsdf.inputs["Base Color"].default_value = lin
            bsdf.inputs["Roughness"].default_value = 0.7
            bsdf.inputs["Metallic"].default_value = METALLIC.get(key, 0.0)
        MATS[key] = mat


# --------------------------------------------------------------------------- builder
def _basis(axis):
    axis = Vector(axis).normalized()
    ref = Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((1, 0, 0))
    u = axis.cross(ref).normalized()
    return axis, u, axis.cross(u)


class Mesh:
    """Accumulates primitives into a single bmesh; finish() turns it into an object."""

    def __init__(self, name):
        self.name, self.bm, self.mats, self.sockets = name, bmesh.new(), [], {}

    def _mi(self, m):
        if m not in self.mats:
            self.mats.append(m)
        return self.mats.index(m)

    def _face(self, verts, m):
        f = self.bm.faces.new(verts)
        f.material_index = self._mi(m)
        f.smooth = False
        return f

    # side profile [(u, v)] extruded between d0..d1
    #   plane 'xz': (x, z) profile, extruded along y   (gun side silhouettes)
    #   plane 'xy': (x, y) profile, extruded along z
    #   plane 'yz': (y, z) profile, extruded along x
    def prism(self, pts, d0, d1, m, plane="xz"):
        def P(u, v, d):
            return {"xz": (u, d, v), "xy": (u, v, d), "yz": (d, u, v)}[plane]
        a = [self.bm.verts.new(P(u, v, d0)) for u, v in pts]
        b = [self.bm.verts.new(P(u, v, d1)) for u, v in pts]
        self._face(a[::-1], m)
        self._face(b, m)
        n = len(pts)
        for i in range(n):
            j = (i + 1) % n
            self._face([a[i], a[j], b[j], b[i]], m)

    def prism_sym(self, pts, width, m, plane="xz", c=0.0):
        self.prism(pts, c - width / 2, c + width / 2, m, plane)

    def box(self, x0, x1, z0, z1, w, m, y=0.0):
        self.prism([(x0, z0), (x1, z0), (x1, z1), (x0, z1)], y - w / 2, y + w / 2, m)

    def lathe(self, prof, p0, axis, m, seg=8, rot=None, mats=None):
        """Revolve [(t, r)] around axis starting at p0. r == 0 makes a pole."""
        axis, u, v = _basis(axis)
        p0 = Vector(p0)
        rot = math.pi / seg if rot is None else rot
        rings = []
        for t, r in prof:
            c = p0 + axis * t
            if r < 1e-6:
                rings.append([self.bm.verts.new(c)])
            else:
                rings.append([self.bm.verts.new(c + (u * math.cos(rot + 2 * math.pi * i / seg) +
                                                     v * math.sin(rot + 2 * math.pi * i / seg)) * r)
                              for i in range(seg)])
        for k in range(len(rings) - 1):
            A, B = rings[k], rings[k + 1]
            mm = mats[k] if mats else m
            if len(A) == 1 and len(B) == 1:
                continue
            for i in range(seg):
                j = (i + 1) % seg
                if len(A) == 1:
                    self._face([A[0], B[i], B[j]], mm)
                elif len(B) == 1:
                    self._face([A[i], A[j], B[0]], mm)
                else:
                    self._face([A[i], A[j], B[j], B[i]], mm)
        if len(rings[0]) > 1:
            self._face(rings[0][::-1], mats[0] if mats else m)
        if len(rings[-1]) > 1:
            self._face(rings[-1], mats[-1] if mats else m)

    def cyl(self, p0, p1, r, m, seg=8, r1=None, rot=None):
        p0, p1 = Vector(p0), Vector(p1)
        self.lathe([(0, r), ((p1 - p0).length, r if r1 is None else r1)], p0, p1 - p0, m, seg, rot)

    def sphere(self, c, r, m, seg=6):
        c = Vector(c)
        self.lathe([(0, 0), (r * 0.3, r * 0.75), (r, r), (r * 1.7, r * 0.75), (2 * r, 0)],
                   c - Vector((0, 0, r)), (0, 0, 1), m, seg)

    def ring(self, c, axis, R, r, m, seg=8):
        axis, u, v = _basis(axis)
        c = Vector(c)
        pts = [c + (u * math.cos(2 * math.pi * i / seg) + v * math.sin(2 * math.pi * i / seg)) * R
               for i in range(seg)]
        for i in range(seg):
            self.cyl(pts[i], pts[(i + 1) % seg], r, m, seg=4)

    def socket(self, name, pos):
        self.sockets[name] = Vector(pos)

    def finish(self, coll, offset=(0, 0, 0)):
        bm, off = self.bm, Vector(offset)
        if off.length:
            bmesh.ops.translate(bm, verts=bm.verts, vec=off)
        ngons = [f for f in bm.faces if len(f.verts) > 4]
        bmesh.ops.triangulate(bm, faces=ngons, quad_method="BEAUTY", ngon_method="EAR_CLIP")
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        # simple box-projected UVs (flat-colour materials; Unreal generates lightmap UVs)
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
        for sname, pos in self.sockets.items():
            e = bpy.data.objects.new("SOCKET_" + sname, None)
            e.empty_display_type = "ARROWS"
            e.empty_display_size = 0.05
            e["socket_name"] = sname          # exact name restored at FBX export time
            coll.objects.link(e)
            e.parent = ob
            e.location = pos + off
        return ob


# --------------------------------------------------------------------------- rifles
def rifle(name, butt, muzzle, forend, handguard, bands, receiver="round", bolt="bent",
          pistol_grip=True, mag=None, gas=None, cleaning_rod=False, rear_sight_x=None,
          wood="Wood"):
    w, Z, B = Mesh(name), 0.04, butt
    grip = ([(-0.14, -0.035), (-0.09, -0.07), (-0.05, -0.065), (-0.035, -0.02)] if pistol_grip
            else [(-0.14, -0.038), (-0.03, -0.025)])
    stock = ([(B, -0.10)] + grip +
             [(0.10, -0.008), (forend, 0.004), (forend, 0.03), (-0.11, 0.03),
              (-0.16, 0.025), (B + 0.04, 0.035), (B, 0.03)])
    w.prism_sym(stock, 0.042, wood)
    w.box(B - 0.006, B, -0.10, 0.03, 0.044, "Metal")                        # butt plate
    if receiver == "round":
        w.cyl((-0.11, 0, Z), (0.09, 0, Z), 0.015, "Metal")
    else:
        w.box(-0.12, 0.10, 0.022, 0.058, 0.03, "Metal")
    w.cyl((0.09, 0, Z), (muzzle, 0, Z), 0.009, "Metal", seg=6)               # barrel
    w.box(handguard[0], handguard[1], 0.028, 0.054, 0.032, wood)             # upper handguard
    for b in bands:
        w.box(b - 0.01, b + 0.01, -0.018, 0.056, 0.046, "Metal")
    # sights
    w.box(muzzle - 0.04, muzzle - 0.005, Z + 0.004, Z + 0.013, 0.014, "Metal")
    w.box(muzzle - 0.03, muzzle - 0.012, Z, Z + 0.026, 0.005, "Metal")
    rx = handguard[0] - 0.02 if rear_sight_x is None else rear_sight_x
    w.box(rx - 0.03, rx + 0.01, Z + 0.008, Z + 0.03, 0.02, "Metal")
    # trigger group
    w.box(-0.05, 0.06, -0.035, -0.012, 0.016, "Metal")
    w.box(-0.025, 0.03, -0.055, -0.048, 0.01, "Metal")
    w.box(0.025, 0.032, -0.055, -0.03, 0.01, "Metal")
    w.box(-0.004, 0.004, -0.05, -0.03, 0.006, "Metal")
    # bolt / charging handle
    if bolt == "bent":
        w.cyl((-0.08, 0.012, Z + 0.004), (-0.09, 0.055, Z - 0.03), 0.005, "Metal", seg=6)
        w.sphere((-0.09, 0.058, Z - 0.033), 0.011, "Metal")
    elif bolt == "straight":
        w.cyl((-0.08, 0.012, Z), (-0.08, 0.06, Z), 0.005, "Metal", seg=6)
        w.sphere((-0.08, 0.064, Z), 0.01, "Metal")
    elif bolt == "enfield":
        w.cyl((-0.06, 0.012, Z + 0.004), (-0.085, 0.05, Z - 0.02), 0.005, "Metal", seg=6)
        w.sphere((-0.088, 0.054, Z - 0.022), 0.01, "Metal")
    elif bolt == "garand":
        w.box(0.0, 0.035, Z - 0.002, Z + 0.012, 0.012, "Metal", y=0.021)
        w.cyl((0.018, 0.026, Z + 0.005), (0.018, 0.04, Z + 0.005), 0.006, "Metal", seg=6)
    elif bolt == "g43":
        w.cyl((-0.03, -0.015, Z + 0.008), (-0.03, -0.04, Z + 0.008), 0.005, "Metal", seg=6)
        w.sphere((-0.03, -0.044, Z + 0.008), 0.008, "Metal")
    if mag:
        x0, x1, depth = mag
        w.box(x0, x1, -0.015 - depth, -0.01, 0.03, "Metal")
    if gas == "garand":
        w.cyl((muzzle - 0.14, 0, Z - 0.018), (muzzle - 0.005, 0, Z - 0.018), 0.01, "Metal")
        w.box(muzzle - 0.035, muzzle - 0.005, Z - 0.026, Z + 0.01, 0.022, "Metal")
    elif gas == "g43":
        w.cyl((0.10, 0, Z + 0.02), (0.42, 0, Z + 0.02), 0.008, "Metal", seg=6)
        w.box(0.40, 0.43, Z - 0.01, Z + 0.03, 0.02, "Metal")
    if cleaning_rod:
        w.cyl((forend, 0, Z - 0.025), (forend + 0.05, 0, Z - 0.025), 0.003, "Metal", seg=6)
    w.socket("Muzzle", (muzzle, 0, Z))
    return w


# --------------------------------------------------------------------------- SMGs / MGs
def mp40():
    w, z = Mesh("SM_MP40"), 0.045
    w.cyl((-0.15, 0, z), (0.20, 0, z), 0.018, "Metal")                       # receiver tube
    w.cyl((0.20, 0, z), (0.23, 0, z), 0.014, "Metal")                        # barrel nut
    w.cyl((0.23, 0, z), (0.33, 0, z), 0.008, "Metal", seg=6)                 # barrel
    w.box(0.20, 0.27, 0.018, 0.032, 0.012, "Metal")                          # resting bar
    w.box(-0.10, 0.07, 0.0, 0.03, 0.026, "Bakelite")                         # lower frame
    w.prism_sym([(-0.035, 0.005), (0.02, 0.005), (0.005, -0.10), (-0.05, -0.095)], 0.03, "Bakelite")
    w.box(-0.005, 0.05, -0.03, -0.02, 0.012, "Metal")
    w.box(0.045, 0.055, -0.03, 0.005, 0.012, "Metal")
    w.box(0.06, 0.11, -0.04, 0.03, 0.032, "Metal")                           # mag housing
    w.box(0.065, 0.105, -0.25, -0.04, 0.026, "Metal")                        # magazine
    w.cyl((0.02, -0.018, 0.05), (0.02, -0.04, 0.05), 0.005, "Metal", seg=6)  # cocking knob
    w.box(-0.06, -0.04, 0.06, 0.075, 0.012, "Metal")
    w.box(0.31, 0.32, 0.05, 0.068, 0.004, "Metal")
    for y in (-0.014, 0.014):                                                # extended stock
        w.cyl((-0.10, y, 0.015), (-0.45, y, -0.01), 0.005, "Metal", seg=6)
    w.box(-0.47, -0.45, -0.06, 0.02, 0.04, "Metal")
    w.socket("Muzzle", (0.33, 0, z))
    return w


def thompson():
    w, z = Mesh("SM_ThompsonM1928"), 0.05
    w.box(-0.10, 0.14, 0.025, 0.075, 0.034, "Metal")
    w.box(-0.08, 0.10, 0.0, 0.03, 0.03, "Metal")
    w.prism_sym([(-0.03, 0.005), (0.025, 0.005), (0.005, -0.095), (-0.05, -0.09)], 0.032, "Wood")
    w.box(0.0, 0.045, -0.03, -0.02, 0.012, "Metal")
    w.cyl((0.14, 0, z), (0.30, 0, z), 0.011, "Metal")                        # finned barrel
    for i in range(6):
        x = 0.15 + i * 0.022
        w.cyl((x, 0, z), (x + 0.008, 0, z), 0.017, "Metal")
    w.box(0.30, 0.35, z - 0.016, z + 0.016, 0.03, "Metal")                   # compensator
    w.prism_sym([(0.17, 0.03), (0.23, 0.03), (0.225, -0.06), (0.18, -0.065)], 0.034, "Wood")
    w.prism_sym([(-0.10, 0.07), (-0.10, 0.02), (-0.44, -0.08), (-0.46, -0.08), (-0.46, 0.06),
                 (-0.40, 0.065)], 0.04, "Wood")                              # stock
    # drum: round face points forward along the barrel (+X), per art direction
    w.cyl((0.07, 0, -0.055), (0.12, 0, -0.055), 0.075, "Metal", seg=10)
    w.cyl((0.12, 0, -0.055), (0.128, 0, -0.055), 0.02, "MetalLight", seg=6)  # winding key hub
    w.cyl((0.04, 0, 0.075), (0.04, 0, 0.087), 0.006, "Metal", seg=6)
    w.box(-0.06, -0.04, 0.075, 0.09, 0.014, "Metal")
    w.box(0.33, 0.34, 0.066, 0.076, 0.004, "Metal")
    w.socket("Muzzle", (0.35, 0, z))
    return w


def ppsh41():
    w, z = Mesh("SM_PPSh41"), 0.045
    w.prism_sym([(0.04, 0.025), (0.04, -0.02), (0.0, -0.03), (-0.05, -0.04), (-0.15, -0.035),
                 (-0.45, -0.11), (-0.47, -0.11), (-0.47, 0.04), (-0.42, 0.045), (-0.15, 0.03),
                 (-0.06, 0.028)], 0.042, "Wood")
    w.box(-0.10, 0.08, 0.025, 0.065, 0.034, "Metal")
    w.cyl((0.08, 0, z), (0.33, 0, z), 0.02, "Metal")                         # barrel jacket
    for i in range(5):
        x = 0.11 + i * 0.04
        w.box(x, x + 0.022, z - 0.008, z + 0.008, 0.042, "Bakelite")         # vent slots
    w.prism_sym([(0.33, z - 0.02), (0.35, z - 0.02), (0.37, z + 0.02), (0.33, z + 0.02)], 0.04, "Metal")
    w.box(-0.03, 0.035, -0.048, -0.038, 0.012, "Metal")
    w.box(0.03, 0.037, -0.048, -0.02, 0.012, "Metal")
    # drum: round face points forward along the barrel (+X), per art direction
    w.cyl((0.088, 0, -0.05), (0.132, 0, -0.05), 0.07, "Metal", seg=10)
    w.cyl((0.132, 0, -0.05), (0.14, 0, -0.05), 0.018, "MetalLight", seg=6)
    w.box(0.095, 0.125, 0.0, 0.03, 0.03, "Metal")                           # drum feed neck
    w.box(-0.06, -0.03, 0.065, 0.078, 0.014, "Metal")
    w.box(0.34, 0.35, 0.062, 0.072, 0.004, "Metal")
    w.socket("Muzzle", (0.37, 0, z))
    return w


def stg44():
    w, z = Mesh("SM_StG44"), 0.045
    w.box(-0.14, 0.20, 0.025, 0.075, 0.034, "Metal")                         # receiver
    w.box(-0.07, 0.06, 0.0, 0.03, 0.03, "Metal")                             # trigger housing
    w.prism_sym([(-0.035, 0.005), (0.015, 0.005), (-0.005, -0.09), (-0.055, -0.085)], 0.032, "Wood")
    w.box(-0.01, 0.05, -0.03, -0.02, 0.012, "Metal")
    w.prism_sym([(-0.14, 0.072), (-0.14, 0.03), (-0.48, -0.06), (-0.50, -0.06), (-0.50, 0.055),
                 (-0.44, 0.065)], 0.04, "Wood")                              # stock
    w.cyl((0.20, 0, 0.068), (0.42, 0, 0.068), 0.012, "Metal")                # gas tube
    w.cyl((0.20, 0, z), (0.48, 0, z), 0.009, "Metal", seg=6)                 # barrel
    w.cyl((0.46, 0, z), (0.48, 0, z), 0.011, "Metal", seg=6)
    w.box(0.40, 0.43, z - 0.012, 0.09, 0.02, "Metal")                        # front sight block
    w.box(0.035, 0.095, -0.02, 0.03, 0.032, "Metal")                         # mag well
    w.prism_sym([(0.04, 0.0), (0.09, 0.0), (0.10, -0.08), (0.13, -0.20), (0.085, -0.215),
                 (0.055, -0.09)], 0.026, "Metal")                            # curved magazine
    w.box(-0.02, 0.04, 0.075, 0.088, 0.02, "Metal")
    w.cyl((0.12, -0.017, 0.06), (0.12, -0.04, 0.06), 0.005, "Metal", seg=6)
    w.socket("Muzzle", (0.48, 0, z))
    return w


def bar():
    w, z = Mesh("SM_BAR_M1918"), 0.055
    w.box(-0.18, 0.20, 0.015, 0.075, 0.036, "Metal")                         # receiver
    w.prism_sym([(-0.18, 0.07), (-0.18, 0.01), (-0.27, -0.01), (-0.46, -0.08), (-0.48, -0.08),
                 (-0.48, 0.06), (-0.42, 0.07)], 0.042, "Wood")               # stock
    w.prism_sym([(-0.035, 0.015), (0.015, 0.015), (-0.01, -0.08), (-0.06, -0.075)], 0.032, "Wood")
    w.box(-0.01, 0.04, -0.02, -0.01, 0.012, "Metal")
    w.box(0.05, 0.12, -0.08, 0.015, 0.03, "Metal")                           # magazine
    w.prism_sym([(0.20, 0.065), (0.42, 0.06), (0.42, 0.0), (0.20, -0.005)], 0.042, "Wood")  # forend
    w.cyl((0.42, 0, 0.025), (0.65, 0, 0.025), 0.01, "Metal")                 # gas tube
    w.cyl((0.20, 0, z), (0.70, 0, z), 0.011, "Metal")                        # barrel
    w.cyl((0.70, 0, z), (0.74, 0, z), 0.011, "Metal", r1=0.016)              # flash hider
    w.box(0.62, 0.64, z, z + 0.03, 0.006, "Metal")
    w.box(-0.17, -0.12, 0.075, 0.095, 0.022, "Metal")
    w.socket("Muzzle", (0.74, 0, z))
    return w


def mg42():
    w, z = Mesh("SM_MG42"), 0.06
    w.box(-0.15, 0.16, 0.02, 0.09, 0.05, "Metal")                            # receiver
    w.box(-0.12, 0.10, 0.09, 0.105, 0.055, "Metal")                          # feed cover
    w.cyl((0.16, 0, z), (0.55, 0, z), 0.028, "Metal")                        # barrel jacket
    for i in range(5):
        x = 0.19 + i * 0.07
        w.box(x, x + 0.04, z - 0.012, z + 0.012, 0.058, "Bakelite")          # cooling slots
    w.cyl((0.55, 0, z), (0.60, 0, z), 0.03, "Metal", r1=0.024)               # muzzle booster
    w.cyl((0.60, 0, z), (0.64, 0, z), 0.024, "Metal", r1=0.014)
    w.box(0.50, 0.51, 0.085, 0.11, 0.006, "Metal")
    w.box(0.10, 0.14, 0.105, 0.115, 0.02, "Metal")
    w.prism_sym([(-0.15, 0.085), (-0.15, 0.03), (-0.30, 0.0), (-0.52, -0.03), (-0.55, -0.03),
                 (-0.55, 0.07), (-0.50, 0.08)], 0.045, "Wood")               # stock
    w.prism_sym([(-0.03, 0.02), (0.02, 0.02), (0.0, -0.08), (-0.05, -0.075)], 0.032, "Wood")
    w.box(-0.06, 0.06, 0.0, 0.025, 0.035, "Metal")
    w.box(-0.005, 0.045, -0.025, -0.015, 0.012, "Metal")
    for s in (-1, 1):                                                        # bipod
        w.cyl((0.48, s * 0.012, 0.035), (0.50, s * 0.10, -0.25), 0.007, "Metal", seg=6)
        w.box(0.49, 0.52, -0.26, -0.245, 0.03, "Metal", y=s * 0.10)
    w.box(0.47, 0.49, 0.02, 0.04, 0.035, "Metal")
    w.socket("Muzzle", (0.64, 0, z))
    return w


def m1919a6():
    """Browning M1919A6: the squad-carried .30 cal with bipod, shoulder stock and carrying handle."""
    w, z = Mesh("SM_M1919A6"), 0.065
    w.box(-0.10, 0.40, 0.0, 0.12, 0.055, "Metal")                            # receiver
    w.box(-0.06, 0.30, 0.12, 0.135, 0.05, "Metal")                           # top cover
    w.box(-0.12, -0.10, 0.005, 0.115, 0.05, "Metal")                         # back plate
    w.cyl((0.40, 0, z), (0.80, 0, z), 0.032, "Metal")                        # perforated barrel jacket
    for i in range(6):
        x = 0.43 + i * 0.058
        w.box(x, x + 0.03, z - 0.01, z + 0.01, 0.068, "Bakelite")            # jacket holes
    w.cyl((0.80, 0, z), (0.90, 0, z), 0.012, "Metal")                        # barrel
    w.cyl((0.90, 0, z), (1.00, 0, z), 0.016, "Metal", r1=0.03)               # conical flash hider
    w.box(0.76, 0.78, z + 0.03, z + 0.06, 0.006, "Metal")                    # front sight
    w.box(0.00, 0.02, 0.135, 0.165, 0.02, "Metal")                           # rear sight
    for x in (0.14, 0.30):                                                   # carrying handle
        w.box(x, x + 0.02, 0.135, 0.19, 0.012, "Metal")
    w.box(0.14, 0.32, 0.185, 0.205, 0.018, "Wood")
    w.prism_sym([(-0.03, 0.0), (0.02, 0.0), (0.0, -0.10), (-0.05, -0.095)], 0.032, "Wood")  # pistol grip
    w.box(-0.005, 0.06, -0.035, -0.025, 0.012, "Metal")                      # trigger guard
    w.prism_sym([(-0.12, 0.10), (-0.12, 0.03), (-0.30, 0.0), (-0.50, -0.03), (-0.54, -0.03),
                 (-0.54, 0.09), (-0.48, 0.10)], 0.035, "Metal")              # shoulder stock
    w.box(-0.545, -0.53, -0.04, 0.10, 0.05, "Bakelite")                      # butt plate
    w.box(0.04, 0.22, -0.20, 0.02, 0.09, "Olive", y=0.105)                   # ammo can (feeds from the left)
    w.box(0.10, 0.17, 0.02, 0.09, 0.03, "Brass", y=0.07)                     # belt
    for s in (-1, 1):                                                        # bipod
        w.cyl((0.74, s * 0.015, z - 0.03), (0.77, s * 0.11, -0.25), 0.008, "Metal", seg=6)
        w.box(0.755, 0.785, -0.26, -0.245, 0.03, "Metal", y=s * 0.11)
    w.box(0.72, 0.76, z - 0.05, z - 0.02, 0.05, "Metal")                     # bipod clamp
    w.socket("Muzzle", (1.00, 0, z))
    return w


def m2_browning():
    w, z = Mesh("SM_M2Browning_Tripod"), 0.03
    w.box(-0.40, 0.20, -0.07, 0.09, 0.11, "Metal")                           # receiver
    w.box(-0.30, 0.10, 0.09, 0.105, 0.10, "Metal")                           # top cover
    w.box(-0.05, 0.05, 0.0, 0.06, 0.02, "Metal", y=-0.065)                   # feed tray
    w.cyl((0.20, 0, z), (0.42, 0, z), 0.045, "Metal")                        # barrel support
    for i in range(3):
        x = 0.24 + i * 0.055
        w.box(x, x + 0.03, z - 0.015, z + 0.015, 0.092, "Bakelite")
    w.cyl((0.42, 0, z), (1.25, 0, z), 0.026, "Metal", r1=0.02)               # heavy barrel
    w.box(0.17, 0.19, 0.105, 0.14, 0.01, "Metal")
    w.box(-0.28, -0.26, 0.105, 0.15, 0.02, "Metal")
    w.box(-0.42, -0.40, -0.06, 0.08, 0.10, "Metal")                          # back plate
    for s in (-1, 1):                                                        # spade grips
        w.cyl((-0.47, s * 0.045, -0.05), (-0.47, s * 0.045, 0.05), 0.014, "Wood")
        w.box(-0.47, -0.42, 0.045, 0.06, 0.012, "Metal", y=s * 0.045)
        w.box(-0.47, -0.42, -0.06, -0.045, 0.012, "Metal", y=s * 0.045)
    w.box(-0.46, -0.43, -0.01, 0.01, 0.05, "Metal")                          # butterfly trigger
    w.box(-0.15, -0.05, 0.0, 0.03, 0.02, "Metal", y=0.065)                   # charging handle
    w.cyl((-0.06, 0.075, 0.015), (-0.06, 0.095, 0.015), 0.01, "Metal", seg=6)
    w.box(-0.06, 0.06, -0.12, -0.07, 0.06, "Metal")                          # cradle
    w.cyl((0, 0, -0.12), (0, 0, -0.30), 0.02, "Metal")                       # pintle
    w.cyl((0, 0, -0.30), (0, 0, -0.35), 0.05, "Metal")                       # tripod head
    head, ground = Vector((0, 0, -0.33)), -0.75
    feet = [Vector((0.75, 0, ground)), Vector((-0.55, -0.45, ground)), Vector((-0.55, 0.45, ground))]
    for f in feet:
        w.cyl(head, f, 0.016, "Olive", seg=6)
        w.box(f.x - 0.03, f.x + 0.03, ground, ground + 0.012, 0.05, "Olive", y=f.y)
    a, b = head.lerp(feet[1], 0.4), head.lerp(feet[2], 0.4)
    w.cyl(a, b, 0.012, "Olive", seg=6)                                       # T&E bar
    w.cyl((-0.30, 0, -0.07), ((a + b) / 2), 0.012, "Metal", seg=6)
    w.socket("Muzzle", (1.25, 0, z))
    return w, (0, 0, -ground)


# --------------------------------------------------------------------------- pistols
def colt1911():
    w, z = Mesh("SM_Colt1911"), 0.05
    w.box(-0.04, 0.165, 0.035, 0.068, 0.024, "Metal")                        # slide
    w.box(-0.04, 0.13, 0.018, 0.036, 0.022, "Metal")                         # frame
    w.prism_sym([(-0.045, 0.02), (0.01, 0.02), (-0.005, -0.10), (-0.06, -0.095)], 0.022, "Metal")
    w.prism_sym([(-0.04, 0.01), (0.005, 0.01), (-0.007, -0.085), (-0.053, -0.082)], 0.03, "Wood")
    w.box(0.0, 0.05, 0.0, 0.007, 0.01, "Metal")                              # trigger guard
    w.box(0.044, 0.051, 0.0, 0.02, 0.01, "Metal")
    w.box(0.012, 0.018, 0.0, 0.02, 0.006, "Metal")                           # trigger
    w.box(-0.052, -0.04, 0.052, 0.072, 0.008, "Metal")                       # hammer
    w.cyl((0.163, 0, z), (0.168, 0, z), 0.01, "Metal")
    w.box(0.15, 0.16, 0.068, 0.073, 0.004, "Metal")
    w.box(-0.035, -0.025, 0.068, 0.074, 0.012, "Metal")
    w.socket("Muzzle", (0.168, 0, z))
    return w


def luger():
    w, z = Mesh("SM_LugerP08"), 0.048
    w.cyl((0.04, 0, z), (0.17, 0, z), 0.009, "Metal")                        # barrel
    w.box(-0.06, 0.06, 0.03, 0.06, 0.022, "Metal")                           # receiver
    w.box(-0.075, 0.0, 0.06, 0.072, 0.016, "Metal")                          # toggle
    w.cyl((-0.06, -0.02, 0.065), (-0.06, 0.02, 0.065), 0.009, "Metal")
    w.box(-0.04, 0.05, 0.015, 0.035, 0.02, "Metal")
    w.prism_sym([(-0.035, 0.02), (0.015, 0.02), (-0.02, -0.085), (-0.07, -0.075)], 0.022, "Metal")
    w.prism_sym([(-0.03, 0.008), (0.007, 0.008), (-0.022, -0.07), (-0.062, -0.064)], 0.03, "WoodDark")
    w.box(0.0, 0.05, 0.0, 0.007, 0.01, "Metal")
    w.box(0.044, 0.051, 0.0, 0.02, 0.01, "Metal")
    w.box(0.012, 0.018, 0.0, 0.02, 0.006, "Metal")
    w.box(0.16, 0.167, z + 0.008, z + 0.018, 0.004, "Metal")
    w.socket("Muzzle", (0.17, 0, z))
    return w


# --------------------------------------------------------------------------- AT weapons
def panzerfaust():
    w = Mesh("SM_Panzerfaust60")
    w.cyl((-0.55, 0, 0), (0.25, 0, 0), 0.025, "Tan")                         # launch tube
    w.lathe([(0, 0.022), (0.06, 0.06), (0.11, 0.075), (0.14, 0.07), (0.25, 0.025), (0.26, 0)],
            (0.22, 0, 0), (1, 0, 0), "Tan", seg=10)                          # warhead
    w.box(0.11, 0.12, 0.02, 0.08, 0.03, "Tan")                               # folding sight
    w.box(0.0, 0.06, 0.02, 0.037, 0.015, "Tan")                              # trigger housing
    w.box(0.045, 0.055, 0.037, 0.05, 0.01, "Metal")                          # trigger lever
    w.socket("Muzzle", (0.25, 0, 0))
    w.socket("Backblast", (-0.55, 0, 0))
    return w


def panzerschreck():
    w = Mesh("SM_Panzerschreck")
    w.cyl((-0.85, 0, 0), (0.79, 0, 0), 0.045, "Olive", seg=10)               # tube
    w.ring((0.79, 0, 0), (1, 0, 0), 0.05, 0.006, "Olive", seg=10)            # muzzle ring
    w.ring((-0.92, 0, 0), (1, 0, 0), 0.05, 0.005, "Metal", seg=8)            # rear wire guard
    for a in range(4):
        ang = a * math.pi / 2 + math.pi / 4
        p = Vector((0, math.cos(ang) * 0.05, math.sin(ang) * 0.05))
        w.cyl(Vector((-0.85, 0, 0)) + p * 0.9, Vector((-0.92, 0, 0)) + p, 0.004, "Metal", seg=4)
    w.prism_sym([(-0.30, -0.04), (-0.18, -0.04), (-0.20, -0.14), (-0.30, -0.15)], 0.035, "Wood")  # shoulder rest
    for x in (0.0, 0.25):                                                     # grips
        w.prism_sym([(x - 0.02, -0.04), (x + 0.025, -0.04), (x + 0.02, -0.13), (x - 0.025, -0.13)], 0.03, "Wood")
    w.box(0.03, 0.08, -0.08, -0.072, 0.01, "Metal")                          # trigger guard
    w.box(0.075, 0.082, -0.08, -0.04, 0.01, "Metal")
    for s in (-1, 1):                                                         # front sight frame
        w.box(0.545, 0.555, 0.0, 0.11, 0.006, "Metal", y=s * 0.035)
    w.box(0.545, 0.555, 0.104, 0.112, 0.076, "Metal")
    w.box(-0.08, -0.02, 0.04, 0.075, 0.012, "Metal", y=-0.03)                # rear sight
    w.socket("Muzzle", (0.79, 0, 0))
    w.socket("Backblast", (-0.85, 0, 0))
    return w


def rpzb_rocket():
    w = Mesh("SM_Panzerschreck_Rocket")
    w.cyl((-0.33, 0, 0), (0.0, 0, 0), 0.015, "Metal")                        # tail boom
    w.cyl((-0.33, 0, 0), (-0.30, 0, 0), 0.042, "Metal", seg=10)              # fin ring
    w.prism_sym([(-0.33, -0.04), (-0.20, -0.012), (-0.20, 0.012), (-0.33, 0.04)], 0.003, "Metal")
    w.prism_sym([(-0.33, -0.04), (-0.20, -0.012), (-0.20, 0.012), (-0.33, 0.04)], 0.003, "Metal", plane="xy")
    w.lathe([(0, 0.015), (0.03, 0.044), (0.16, 0.044), (0.28, 0.022), (0.31, 0.008), (0.32, 0)],
            (0, 0, 0), (1, 0, 0), "Olive", seg=10,
            mats=["Olive", "Olive", "Olive", "Brass", "Brass"])
    return w


# --------------------------------------------------------------------------- mortar
def mortar_shell():
    w = Mesh("SM_MortarShell60mm")
    w.lathe([(0, 0), (0, 0.011), (0.08, 0.011), (0.09, 0.02), (0.12, 0.029), (0.16, 0.03),
             (0.20, 0.027), (0.235, 0.016), (0.245, 0.011), (0.26, 0.006), (0.265, 0)],
            (0, 0, -0.12), (0, 0, 1), "Olive", seg=8,
            mats=["Metal", "Metal", "Metal", "Olive", "Olive", "Olive", "Olive", "Brass", "Brass", "Brass"])
    fin = [(-0.026, -0.12), (0.026, -0.12), (0.012, -0.07), (-0.012, -0.07)]
    w.prism_sym(fin, 0.003, "Metal")
    w.prism_sym(fin, 0.003, "Metal", plane="yz")
    return w


def mortar():
    w = Mesh("SM_Mortar60mm")
    w.box(-0.18, 0.18, 0.0, 0.03, 0.30, "Olive")                             # base plate
    w.cyl((0, 0, 0.02), (0, 0, 0.06), 0.06, "Olive")                         # socket
    el = math.radians(60)
    d = Vector((math.cos(el), 0, math.sin(el)))
    base = Vector((0, 0, 0.05))
    w.cyl(base, base + d * 0.726, 0.035, "Olive")                            # tube
    w.cyl(base - d * 0.02, base + d * 0.04, 0.042, "Olive")                  # breech cap
    w.cyl(base + d * 0.70, base + d * 0.726, 0.04, "Olive")                  # muzzle ring
    c = base + d * 0.50
    w.cyl(c - d * 0.03, c + d * 0.03, 0.046, "Metal")                        # bipod collar
    feet = [Vector((0.55, -0.22, 0.0)), Vector((0.55, 0.22, 0.0))]
    for f in feet:
        w.cyl(c, f, 0.012, "Olive", seg=6)
        w.box(f.x - 0.03, f.x + 0.03, 0.0, 0.012, 0.04, "Olive", y=f.y)
    w.cyl(c.lerp(feet[0], 0.6), c.lerp(feet[1], 0.6), 0.005, "Metal", seg=4)  # spread chain
    col_bot = Vector((0.50, 0, 0.12))
    w.cyl(col_bot, c, 0.016, "Metal")                                        # elevating column
    w.cyl((0.50, 0, 0.0), col_bot, 0.02, "Metal")                           # column foot
    w.cyl((0.52, 0, 0.16), (0.52, 0.07, 0.16), 0.005, "Metal", seg=4)        # crank
    w.box(0.51, 0.53, 0.16, 0.20, 0.012, "Metal", y=0.07)
    w.socket("Muzzle", base + d * 0.726)
    return w


# --------------------------------------------------------------------------- grenades
def mk2_grenade():
    w = Mesh("SM_Mk2Grenade")
    w.lathe([(0, 0), (0.005, 0.015), (0.012, 0.024), (0.025, 0.029), (0.04, 0.026), (0.055, 0.029),
             (0.07, 0.026), (0.085, 0.024), (0.095, 0.016), (0.10, 0.012)],
            (0, 0, -0.055), (0, 0, 1), "Olive", seg=8)
    w.cyl((0, 0, 0.045), (0, 0, 0.07), 0.01, "Metal")                        # fuze
    w.prism_sym([(0.006, 0.07), (0.016, 0.072), (0.036, 0.0), (0.028, -0.01)], 0.012, "Metal")  # spoon
    w.ring((-0.004, -0.02, 0.06), (0, 1, 0), 0.012, 0.0015, "MetalLight", seg=8)  # pin ring
    return w


def stielhandgranate():
    w = Mesh("SM_Stielhandgranate24")
    w.cyl((0, 0, -0.14), (0, 0, 0.13), 0.013, "Wood")                        # handle
    w.cyl((0, 0, -0.15), (0, 0, -0.14), 0.015, "Metal")                      # cap
    w.cyl((0, 0, 0.11), (0, 0, 0.13), 0.015, "Metal")                        # ferrule
    w.lathe([(0, 0.014), (0.008, 0.034), (0.075, 0.035), (0.08, 0.03), (0.08, 0)],
            (0, 0, 0.13), (0, 0, 1), "Olive", seg=10)                         # head
    return w


# --------------------------------------------------------------------------- flamethrower
def flamethrower_tanks():
    w = Mesh("SM_M2Flamethrower_Tanks")
    tank = [(0, 0), (0.02, 0.06), (0.05, 0.085), (0.53, 0.085), (0.56, 0.06), (0.58, 0)]
    for y in (-0.10, 0.10):
        w.lathe(tank, (0, y, 0), (0, 0, 1), "Olive", seg=8)
        w.cyl((0, y, 0.57), (0, y, 0.62), 0.015, "Metal")                    # valve
        w.cyl((0, y, 0.62), (0, y, 0.63), 0.03, "Metal", seg=6)              # hand wheel
        for zz in (0.12, 0.45):
            w.cyl((0, y, zz), (0, y, zz + 0.035), 0.089, "Bakelite")         # straps
    w.lathe([(0, 0), (0.02, 0.04), (0.05, 0.055), (0.32, 0.055), (0.35, 0.04), (0.37, 0)],
            (0.10, 0, 0.10), (0, 0, 1), "Olive", seg=8)                      # pressure tank
    w.box(-0.10, -0.09, 0.05, 0.56, 0.40, "Olive")                           # back frame
    for zz in (0.08, 0.50):
        w.box(-0.09, 0.0, zz, zz + 0.03, 0.30, "Olive")
    w.cyl((0, -0.10, 0.0), (0, -0.10, -0.06), 0.012, "Metal")                # fuel outlet
    w.cyl((0, 0.10, 0.0), (0, 0.10, -0.06), 0.012, "Metal")
    w.cyl((0, -0.10, -0.06), (0, 0.10, -0.06), 0.012, "Metal")
    return w, (0, 0, 0.07)


def flamethrower_wand():
    w = Mesh("SM_M2Flamethrower_Wand")
    w.cyl((-0.25, 0, 0), (0.50, 0, 0), 0.012, "Olive")
    w.cyl((0.50, 0, 0), (0.56, 0, 0), 0.012, "Metal", r1=0.02)              # nozzle
    for x in (0.0, 0.25):
        w.prism_sym([(x - 0.02, -0.01), (x + 0.025, -0.01), (x + 0.015, -0.10), (x - 0.03, -0.10)], 0.03, "Olive")
    w.box(-0.06, 0.06, -0.02, 0.025, 0.035, "Metal")                         # valve body
    w.box(0.025, 0.035, -0.045, -0.015, 0.008, "Metal")                      # trigger
    w.cyl((0.30, 0, -0.028), (0.48, 0, -0.028), 0.016, "Metal")              # igniter
    w.cyl((-0.25, 0, 0), (-0.32, 0, 0), 0.015, "Bakelite")                   # hose stub
    w.socket("Muzzle", (0.56, 0, 0))
    return w


# --------------------------------------------------------------------------- attachments
def scope(name, L, r_tube, r_obj, r_oc):
    w = Mesh(name)
    h = L / 2
    w.lathe([(0, 0), (0, r_oc), (0.05, r_oc), (0.07, r_tube), (L - 0.06, r_tube),
             (L - 0.03, r_obj), (L, r_obj), (L, 0)], (-h, 0, 0), (1, 0, 0), "Metal", seg=10)
    w.cyl((0, 0, r_tube * 0.8), (0, 0, r_tube + 0.016), 0.01, "Metal")       # elevation turret
    w.cyl((0, r_tube * 0.8, 0), (0, r_tube + 0.012, 0), 0.009, "Metal")      # windage turret
    for x in (-h + 0.09, h - 0.08):
        w.box(x - 0.012, x + 0.012, -r_tube - 0.022, -r_tube * 0.5, 0.02, "Metal")
    return w


def bayonet(name, bl, h=0.025, hl=0.11, spike=False):
    w = Mesh(name)
    if spike:
        w.lathe([(0, 0.008), (bl * 0.9, 0.006), (bl, 0)], (0, 0, 0), (1, 0, 0), "Steel", seg=4, rot=math.pi / 4)
        w.cyl((-0.07, 0, 0.03), (0.0, 0, 0.03), 0.011, "Metal")              # socket tube
        w.box(-0.02, 0.005, -0.006, 0.03, 0.01, "Metal")                     # elbow
        return w
    w.prism_sym([(0, -h / 2), (bl - 0.035, -h / 2), (bl, h * 0.2), (bl - 0.05, h / 2), (0, h / 2)],
                0.005, "Steel")                                              # blade
    w.box(-0.008, 0.0, -h / 2 - 0.012, h / 2 + 0.02, 0.012, "Metal")         # crossguard
    w.cyl((-0.008, 0, h / 2 + 0.017), (0.004, 0, h / 2 + 0.017), 0.009, "Metal")  # muzzle ring
    w.prism_sym([(-0.008, -h / 2 - 0.006), (-hl, -h / 2 - 0.01), (-hl, h / 2 + 0.004),
                 (-0.008, h / 2 + 0.004)], 0.018, "Wood")                    # grip
    w.box(-hl - 0.014, -hl, -h / 2 - 0.012, h / 2 + 0.006, 0.02, "Metal")    # pommel
    return w


def _round(w, x, y, case, bullet, r):
    w.lathe([(0, 0), (0, r), (case - 0.006, r), (case, r * 0.75), (case, r * 0.7),
             (case + bullet * 0.6, r * 0.68), (case + bullet, 0)],
            (x, y, 0), (0, 0, 1), "Brass", seg=6,
            mats=["Brass", "Brass", "Brass", "Brass", "Copper", "Copper"])


def stripper_clip():
    w = Mesh("SM_Ammo_StripperClip_792")
    for i in range(5):
        _round(w, -0.019 + i * 0.0095, 0, 0.057, 0.023, 0.0059)
    w.box(-0.026, 0.026, -0.002, 0.006, 0.014, "Metal")
    return w


def enbloc_clip():
    w = Mesh("SM_Ammo_EnBloc_3006")
    for i in range(8):
        _round(w, -0.019 + i * 0.0055, 0.0045 if i % 2 else -0.0045, 0.063, 0.021, 0.006)
    for s in (-1, 1):
        w.box(-0.027, 0.0215, 0.0, 0.03, 0.001, "Metal", y=s * 0.0115)
    return w


# --------------------------------------------------------------------------- scene
def build():
    bpy.ops.wm.read_homefile(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    make_materials()
    root = bpy.data.collections.new("Weapons")
    scene.collection.children.link(root)
    cats = {}

    def coll(name):
        if name not in cats:
            c = bpy.data.collections.new(name)
            root.children.link(c)
            cats[name] = c
        return cats[name]

    def add(cat, res):
        w, off = res if isinstance(res, tuple) else (res, (0, 0, 0))
        return w.finish(coll(cat), off)

    O = {}
    for res in [
        rifle("SM_M1Garand", -0.43, 0.67, 0.52, (0.12, 0.53), [0.30], "box", "garand",
              gas="garand", rear_sight_x=-0.09),
        rifle("SM_Gewehr43", -0.42, 0.70, 0.47, (0.10, 0.40), [0.40, 0.47], "box", "g43",
              mag=(0.035, 0.105, 0.075), gas="g43", rear_sight_x=0.06),
        rifle("SM_Kar98k", -0.43, 0.68, 0.62, (0.12, 0.42), [0.42, 0.60], "round", "bent",
              cleaning_rod=True),
        rifle("SM_MosinNagant", -0.44, 0.79, 0.70, (0.12, 0.66), [0.40, 0.66], "round", "straight",
              pistol_grip=False, mag=(0.035, 0.10, 0.025)),
        rifle("SM_LeeEnfieldNo4", -0.43, 0.70, 0.63, (0.10, 0.62), [0.30, 0.62], "round", "enfield",
              mag=(0.035, 0.095, 0.05)),
    ]:
        ob = add("Rifles", res); O[ob.name] = ob
    for f in (mp40, thompson, ppsh41, stg44):
        ob = add("SMGs", f()); O[ob.name] = ob
    for f in (bar, mg42, m1919a6, m2_browning):
        ob = add("MachineGuns", f()); O[ob.name] = ob
    for f in (colt1911, luger):
        ob = add("Pistols", f()); O[ob.name] = ob
    for f in (panzerfaust, panzerschreck, rpzb_rocket, mortar, mortar_shell):
        ob = add("Heavy", f()); O[ob.name] = ob
    for f in (mk2_grenade, stielhandgranate):
        ob = add("Grenades", f()); O[ob.name] = ob
    for f in (flamethrower_tanks, flamethrower_wand):
        ob = add("Flamethrower", f()); O[ob.name] = ob
    for res in [scope("SM_Scope_ZF39", 0.27, 0.013, 0.021, 0.019),
                scope("SM_Scope_M84", 0.30, 0.012, 0.017, 0.017),
                scope("SM_Scope_PU", 0.27, 0.012, 0.014, 0.015),
                bayonet("SM_Bayonet_M1905", 0.41, 0.025, 0.11),
                bayonet("SM_Bayonet_S84_98", 0.25, 0.028, 0.12),
                bayonet("SM_Bayonet_Mosin", 0.43, spike=True),
                bayonet("SM_Bayonet_No4", 0.20, spike=True),
                stripper_clip(), enbloc_clip()]:
        ob = add("Attachments", res); O[ob.name] = ob

    layout(O)
    setup_preview(scene)
    return O


COLUMNS = [
    ["SM_Panzerfaust60", "SM_Panzerschreck", "SM_PPSh41", "SM_MG42", "SM_StG44", "SM_BAR_M1918",
     "SM_ThompsonM1928", "SM_MP40"],
    ["SM_Scope_ZF39", "SM_M1Garand", "SM_Bayonet_M1905", "SM_Gewehr43", "SM_Scope_M84", "SM_Kar98k",
     "SM_Bayonet_S84_98", "SM_MosinNagant", "SM_Scope_PU", "SM_Bayonet_Mosin", "SM_LeeEnfieldNo4",
     "SM_Bayonet_No4", "SM_Ammo_StripperClip_792", "SM_Ammo_EnBloc_3006"],
    ["SM_M2Browning_Tripod", "SM_M1919A6", "SM_Colt1911", "SM_LugerP08", "SM_Panzerschreck_Rocket",
     "SM_Stielhandgranate24", "SM_Mk2Grenade", "SM_MortarShell60mm", "SM_Mortar60mm",
     "SM_M2Flamethrower_Wand", "SM_M2Flamethrower_Tanks"],
]


def _bounds(ob):
    xs = [v.co.x for v in ob.data.vertices]
    zs = [v.co.z for v in ob.data.vertices]
    return min(xs), max(xs), min(zs), max(zs)


def layout(O, gap=0.07, col_gap=0.25):
    x = 0.0
    for col in COLUMNS:
        z, width = 0.0, 0.0
        for name in col:
            ob = O[name]
            x0, x1, z0, z1 = _bounds(ob)
            ob.location = (x - x0, 0, z - z1)
            z -= (z1 - z0) + gap
            width = max(width, x1 - x0)
        x += width + col_gap


def setup_preview(scene):
    bpy.context.view_layer.update()
    objs = [o for o in scene.objects if o.type == "MESH"]
    pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    mn = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    mx = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    ctr = (mn + mx) / 2
    prev = bpy.data.collections.new("Preview")
    scene.collection.children.link(prev)
    cam_data = bpy.data.cameras.new("PreviewCam")
    cam_data.type = "ORTHO"
    scene.render.resolution_x, scene.render.resolution_y = 1920, 1080
    cam_data.ortho_scale = max((mx.x - mn.x), (mx.z - mn.z) * 16 / 9) * 1.06
    cam = bpy.data.objects.new("PreviewCam", cam_data)
    prev.objects.link(cam)
    cam.location = (ctr.x, -10, ctr.z)
    cam.rotation_euler = (math.radians(90), 0, 0)
    scene.camera = cam
    if scene.world is None:
        scene.world = bpy.data.worlds.new("World")
    scene.world.color = (0.035, 0.035, 0.035)
    scene.render.engine = "BLENDER_WORKBENCH"
    sh = scene.display.shading
    sh.light = "STUDIO"
    sh.color_type = "MATERIAL"
    sh.show_cavity = True
    sh.show_object_outline = False
    scene.render.filepath = PREVIEW_PATH


def render_preview():
    bpy.ops.render.render(write_still=True)


def export_fbx(folder=FBX_DIR, only=None):
    os.makedirs(folder, exist_ok=True)
    vl = bpy.context.view_layer
    weapons = [o for o in bpy.data.collections["Weapons"].all_objects if o.type == "MESH" and (not only or o.name in only)]
    # Blender keeps object names unique (SOCKET_Muzzle.001, ...), but Unreal takes the socket
    # name from the FBX node name. Park every socket empty on a temp name, then give each
    # weapon's empties their exact SOCKET_<Name> while that weapon is exported.
    empties = [o for o in bpy.data.objects if "socket_name" in o]
    original = {o: o.name for o in empties}
    for i, o in enumerate(empties):
        o.name = "__sock_tmp_%d" % i
    out = []
    for ob in weapons:
        for c in ob.children:
            if "socket_name" in c:
                c.name = "SOCKET_" + c["socket_name"]
        loc = ob.location.copy()
        ob.location = (0, 0, 0)
        vl.update()
        for o in vl.objects:
            o.select_set(False)
        ob.select_set(True)
        for c in ob.children:
            c.select_set(True)
        vl.objects.active = ob
        path = os.path.join(folder, ob.name + ".fbx")
        sel = [ob] + list(ob.children)
        # explicit override: when run from a timer/socket the context has no selection members
        with bpy.context.temp_override(selected_objects=sel, active_object=ob, object=ob):
            bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH", "EMPTY"},
                                     mesh_smooth_type="FACE", apply_unit_scale=True,
                                     bake_space_transform=False, add_leaf_bones=False,
                                     use_mesh_modifiers=True, axis_forward="-Z", axis_up="Y")
        ob.location = loc
        for i, c in enumerate(ob.children):
            if "socket_name" in c:
                c.name = "__sock_done_%s_%d" % (ob.name, i)
        out.append(path)
    for o, n in original.items():
        o.name = n
    vl.update()
    return out


if __name__ == "__main__" or True:
    objs = build()
    bpy.ops.wm.save_as_mainfile(filepath=BLEND_PATH)
    print("Built %d weapons" % len(objs))
