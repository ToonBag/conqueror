"""Squad Command - stylized faceted low-poly WW2 soldier (modular, rigged).

Run inside Blender (wipes the open scene, then saves to OUT_BLEND):
    exec(compile(open(PATH).read(), PATH, 'exec'), {'__name__': '__main__'})

Modular parts, all skinned to one UE-mannequin-named skeleton (SK_Soldier):
    Body:      CH_Head, CH_Hands
    Outfit:    OUT_Tunic, OUT_Trousers, OUT_Boots       (swap these per uniform)
    Equipment: EQ_Helmet, EQ_Webbing                    (belt, pouches, suspenders, canteen)
    Factions:  Outfit_Allied / Outfit_Axis collections hold *_Allied / *_Axis copies of the outfit and
               equipment meshes with faction materials (see FACTIONS); the neutral base set is hidden.
    Props:     PROP_Rifle, PROP_Grenade on weapon_r / weapon_l (test only)
Units: metres, Z up, character faces -Y. 1 m = 100 UU in Unreal.
"""
import bpy, bmesh, math
from mathutils import Vector, Matrix

OUT_BLEND = r"C:\Users\toony\Projects\Unreal\SourceArt\Characters\SK_WW2_Soldier_Stylized.blend"
ARM_ANGLE = math.radians(40)          # A-pose: arms 40 deg below horizontal
Y = Vector((0, 1, 0))


def ss(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


# --------------------------------------------------------------------------
# Joints
# --------------------------------------------------------------------------
def joints():
    J = {}
    J['pelvis'] = Vector((0, 0, 0.95)); J['spine_01'] = Vector((0, 0, 1.06)); J['spine_02'] = Vector((0, 0, 1.20))
    J['spine_03'] = Vector((0, 0, 1.32)); J['neck_01'] = Vector((0, 0, 1.48)); J['head'] = Vector((0, 0, 1.57))
    J['head_end'] = Vector((0, 0, 1.84))
    for side, s in (('l', 1), ('r', -1)):
        d = Vector((math.cos(ARM_ANGLE) * s, 0, -math.sin(ARM_ANGLE)))
        n = Vector((-math.sin(ARM_ANGLE) * s, 0, -math.cos(ARM_ANGLE)))      # palm normal
        sh = Vector((0.21 * s, 0, 1.44)); el = sh + 0.28 * d; wr = el + 0.25 * d; wr.y = -0.01
        J['d_' + side] = d; J['n_' + side] = n
        J['clavicle_' + side] = Vector((0.03 * s, 0, 1.42)); J['upperarm_' + side] = sh
        J['lowerarm_' + side] = el; J['hand_' + side] = wr; J['palm_end_' + side] = wr + 0.085 * d
        for f, lat, segs in (('index', -0.031, (0.034, 0.024, 0.020)), ('middle', -0.010, (0.037, 0.026, 0.021)),
                             ('ring', 0.011, (0.035, 0.024, 0.020)), ('pinky', 0.031, (0.028, 0.020, 0.017))):
            p = wr + (0.088 - (0.008 if f == 'pinky' else 0)) * d + lat * Y
            cdir = (d + 0.15 * n).normalized(); pts = [p]
            for L in segs:
                pts.append(pts[-1] + L * cdir); cdir = (cdir + 0.22 * n).normalized()
            for i in range(3): J[f'{f}_0{i+1}_{side}'] = pts[i]
            J[f'{f}_tip_{side}'] = pts[3]
        tdir = (0.5 * d - 0.7 * Y * 1 + 0.45 * n).normalized()
        pts = [wr + 0.025 * d - 0.03 * Y + 0.012 * n]
        for L in (0.032, 0.026, 0.022):
            pts.append(pts[-1] + L * tdir); tdir = (tdir + 0.15 * n).normalized()
        for i in range(3): J[f'thumb_0{i+1}_{side}'] = pts[i]
        J['thumb_tip_' + side] = pts[3]
        J['thigh_' + side] = Vector((0.10 * s, 0, 0.90)); J['calf_' + side] = Vector((0.11 * s, -0.012, 0.48))
        J['foot_' + side] = Vector((0.115 * s, 0.01, 0.085)); J['ball_' + side] = Vector((0.118 * s, -0.10, 0.03))
        J['toe_' + side] = Vector((0.12 * s, -0.18, 0.03))
        J['grip_' + side] = wr + 0.05 * d + 0.035 * n
    return J


# --------------------------------------------------------------------------
# Geometry helpers
# --------------------------------------------------------------------------
def P8(fw, bw, fs, bs):
    """Chamfered box profile; y=-1 is the front (or bottom for boots)."""
    return [(fw, -1), (1, -fs), (1, bs), (bw, 1), (-bw, 1), (-1, bs), (-1, -fs), (-fw, -1)]


SQ = [(1, -1), (1, 1), (-1, 1), (-1, -1)]
ROUND10 = [(math.cos(2 * math.pi * k / 10), math.sin(2 * math.pi * k / 10)) for k in range(10)]


def tag(faces, mi):
    for f in faces: f.material_index = mi
    return faces


def vloft(bm, levels, profile, cap0=True, cap1=True, mi=0):
    """Vertical stack: levels = (z, hx, hy, yoff[, xoff])."""
    rings = []
    for lv in levels:
        z, hx, hy, yo = lv[:4]; xo = lv[4] if len(lv) > 4 else 0.0
        rings.append([bm.verts.new((xo + px * hx, yo + py * hy, z)) for px, py in profile])
    return rings, tag(_bridge(bm, rings, cap0, cap1), mi)


def loft(bm, pts, sizes, profile, up, cap0=True, cap1=True, mi=0):
    """Loft along a polyline; profile x -> side axis, y -> 'up' axis."""
    rings = []
    n = len(pts)
    for i, (c, (hx, hy)) in enumerate(zip(pts, sizes)):
        t = (pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]).normalized()
        s = t.cross(up)
        if s.length < 1e-6: s = t.cross(Vector((1, 0, 0)))
        s.normalize(); u = s.cross(t).normalized()
        rings.append([bm.verts.new(c + px * hx * s + py * hy * u) for px, py in profile])
    return rings, tag(_bridge(bm, rings, cap0, cap1), mi)


def _bridge(bm, rings, cap0, cap1):
    m = len(rings[0]); faces = []
    for ra, rb in zip(rings, rings[1:]):
        for j in range(m):
            faces.append(bm.faces.new((ra[j], ra[(j + 1) % m], rb[(j + 1) % m], rb[j])))
    if cap0: faces.append(bm.faces.new(list(reversed(rings[0]))))
    if cap1: faces.append(bm.faces.new(rings[-1]))
    return faces


def box(bm, c, size, mi=0, rot=None):
    r = bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=size, verts=r['verts'])
    if rot is not None: bmesh.ops.rotate(bm, verts=r['verts'], cent=(0, 0, 0), matrix=rot)
    bmesh.ops.translate(bm, vec=c, verts=r['verts'])
    fs = list({f for v in r['verts'] for f in v.link_faces})
    return tag(fs, mi)


def material(name, rgb, rough=0.8, metal=0.0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    b = next(nd for nd in m.node_tree.nodes if nd.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*rgb, 1); b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal; m.diffuse_color = (*rgb, 1)
    return m


def make_obj(name, bm, mats, coll):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    for m in mats: me.materials.append(m)
    for p in me.polygons: p.use_smooth = False
    ob = bpy.data.objects.new(name, me); coll.objects.link(ob)
    return ob


def collection(name):
    c = bpy.data.collections.get(name) or bpy.data.collections.new(name)
    if c.name not in bpy.context.scene.collection.children: bpy.context.scene.collection.children.link(c)
    return c


# --------------------------------------------------------------------------
# Shapes shared by several parts
# --------------------------------------------------------------------------
TORSO = [  # z, hx, hy, yoff
    (0.84, 0.198, 0.148, 0.0), (0.95, 0.186, 0.138, 0.0), (1.06, 0.176, 0.132, 0.0), (1.18, 0.192, 0.138, -0.004),
    (1.31, 0.216, 0.146, -0.010), (1.42, 0.206, 0.130, -0.004), (1.485, 0.130, 0.096, 0.004)]
TORSO_P = P8(0.60, 0.65, 0.45, 0.50)


def torso_at(z):
    for a, b in zip(TORSO, TORSO[1:]):
        if a[0] <= z <= b[0]:
            t = (z - a[0]) / (b[0] - a[0]); return tuple(a[i] + (b[i] - a[i]) * t for i in (1, 2, 3))
    return TORSO[-1][1:] if z > TORSO[-1][0] else TORSO[0][1:]


def front_y(z):
    hx, hy, yo = torso_at(z); return yo - hy


def back_y(z):
    hx, hy, yo = torso_at(z); return yo + hy


HEAD = [  # z, hx, hy, yoff
    (1.545, 0.066, 0.072, -0.004), (1.578, 0.085, 0.093, -0.012), (1.630, 0.095, 0.102, -0.008),
    (1.690, 0.099, 0.105, -0.004), (1.736, 0.099, 0.105, 0.000), (1.786, 0.090, 0.098, 0.004),
    (1.816, 0.064, 0.072, 0.006)]


def head_hx(z):
    for a, b in zip(HEAD, HEAD[1:]):
        if a[0] <= z <= b[0]:
            return a[1] + (b[1] - a[1]) * (z - a[0]) / (b[0] - a[0])
    return HEAD[0][1]
HEAD_P = P8(0.62, 0.70, 0.35, 0.45)


def head_front_y(z):
    for a, b in zip(HEAD, HEAD[1:]):
        if a[0] <= z <= b[0]:
            t = (z - a[0]) / (b[0] - a[0]); return (a[3] + (b[3] - a[3]) * t) - (a[2] + (b[2] - a[2]) * t)
    return HEAD[0][3] - HEAD[0][2]


# --------------------------------------------------------------------------
# Parts
# --------------------------------------------------------------------------
def build_head(J, M, coll):
    bm = bmesh.new()
    vloft(bm, [(1.455, 0.058, 0.060, 0.006), (1.585, 0.058, 0.060, 0.006)], P8(0.6, 0.6, 0.5, 0.5), mi=0)  # neck
    rings, faces = vloft(bm, HEAD, HEAD_P, mi=0)
    top = bm.verts.new((0, 0.006, 1.826))
    # replace flat top cap with a low pyramid for a rounder crown
    bm.faces.remove(faces[-1])
    for j in range(8):
        tag([bm.faces.new((rings[-1][j], rings[-1][(j + 1) % 8], top))], 0)
    # hair: crown + back of head
    for f in bm.faces:
        c = f.calc_center_median()
        if c.z > 1.755 or (c.y > 0.04 and c.z > 1.66) or (abs(c.x) > 0.085 and c.z > 1.725 and c.y > -0.05):
            f.material_index = 1
    fy = head_front_y(1.716)
    box(bm, (0, fy + 0.004, 1.716), (0.124, 0.014, 0.017), 0)                       # heavy brow ridge
    for s in (1, -1):
        box(bm, (0.031 * s, head_front_y(1.695) - 0.0005, 1.696), (0.024, 0.006, 0.011), 2)   # eyes
        box(bm, (0.031 * s, fy - 0.0035, 1.7265), (0.036, 0.006, 0.007), 1,
            Matrix.Rotation(math.radians(-6 * s), 3, 'Y'))                           # brows
        ex = head_hx(1.676) + 0.003
        box(bm, (ex * s, 0.004, 1.676), (0.016, 0.034, 0.05), 0)                     # ears
    nf = head_front_y(1.68)
    top_v = bm.verts.new((0, nf + 0.004, 1.708)); l = bm.verts.new((-0.0135, nf + 0.004, 1.652))
    r = bm.verts.new((0.0135, nf + 0.004, 1.652)); tip = bm.verts.new((0, nf - 0.024, 1.657))
    tag([bm.faces.new((top_v, tip, r)), bm.faces.new((top_v, l, tip)), bm.faces.new((l, r, tip)),
         bm.faces.new((top_v, r, l))], 0)                                              # nose wedge
    box(bm, (0, head_front_y(1.618) - 0.0005, 1.618), (0.036, 0.005, 0.005), 2)        # mouth
    return make_obj("CH_Head", bm, [M['skin'], M['hair'], M['dark']], coll)


def build_hands(J, M, coll):
    bm = bmesh.new()
    for side in 'lr':
        d, n, wr = J['d_' + side], J['n_' + side], J['hand_' + side]
        up = -n
        loft(bm, [wr - 0.03 * d, wr + 0.03 * d, J['palm_end_' + side]], [(0.040, 0.019), (0.046, 0.021), (0.045, 0.018)],
             P8(0.8, 0.8, 0.6, 0.6), up)
        for f in ('index', 'middle', 'ring', 'pinky', 'thumb'):
            ch = [J[f'{f}_0{i}_{side}'] for i in (1, 2, 3)] + [J[f'{f}_tip_{side}']]
            base = ch[0] - 0.012 * (ch[1] - ch[0]).normalized()
            r0 = 0.0125 if f == 'thumb' else 0.0098
            loft(bm, [base] + ch, [(r0, r0), (r0, r0), (r0 * 0.95, r0 * 0.95), (r0 * 0.9, r0 * 0.9), (r0 * 0.8, r0 * 0.8)],
                 SQ, up)
    return make_obj("CH_Hands", bm, [M['skin']], coll)


def build_tunic(J, M, coll):
    bm = bmesh.new()
    vloft(bm, TORSO, TORSO_P)
    # collar (thick ring around the neck)
    vloft(bm, [(1.462, 0.088, 0.084, 0.004), (1.512, 0.080, 0.076, 0.004), (1.512, 0.064, 0.064, 0.006),
               (1.47, 0.064, 0.064, 0.006)], P8(0.6, 0.6, 0.5, 0.5), cap0=False, cap1=False)
    for side in 'lr':
        d, n, sh, el, wr = J['d_' + side], J['n_' + side], J['upperarm_' + side], J['lowerarm_' + side], J['hand_' + side]
        pts = [sh - 0.05 * d + Vector((0, 0, -0.045)), sh + 0.03 * d + Vector((0, 0, -0.012)), sh + 0.15 * d, el,
               el + 0.09 * d, wr - 0.035 * d, wr - 0.012 * d, wr + 0.012 * d]
        sizes = [(0.072, 0.072), (0.079, 0.078), (0.071, 0.071), (0.061, 0.063), (0.059, 0.058),
                 (0.050, 0.048), (0.057, 0.054), (0.057, 0.054)]
        loft(bm, pts, sizes, P8(0.6, 0.6, 0.6, 0.6), -n)
    for s in (1, -1):
        # breast pockets + flaps
        z = 1.33; fy = front_y(z)
        box(bm, (0.075 * s, fy - 0.004, z), (0.07, 0.012, 0.072))
        box(bm, (0.075 * s, fy - 0.010, z + 0.034), (0.076, 0.012, 0.026))
        # skirt pockets + flaps
        z = 0.925; fy = front_y(z)
        box(bm, (0.072 * s, fy - 0.004, z), (0.072, 0.012, 0.075))
        box(bm, (0.072 * s, fy - 0.010, z + 0.036), (0.078, 0.012, 0.025))
        # shoulder straps (epaulettes)
        box(bm, (0.15 * s, 0.0, 1.452), (0.11, 0.035, 0.012), 0, Matrix.Rotation(math.radians(-22 * s), 3, 'Y'))
    # button placket
    box(bm, (0, front_y(1.13) - 0.003, 1.13), (0.022, 0.008, 0.56))
    return make_obj("OUT_Tunic", bm, [M['tunic']], coll)


def build_trousers(J, M, coll):
    bm = bmesh.new()
    vloft(bm, [(1.03, 0.166, 0.126, 0.0), (0.92, 0.176, 0.132, 0.0), (0.81, 0.15, 0.10, 0.0)], TORSO_P)
    for side, s in (('l', 1), ('r', -1)):
        th, kn, ft = J['thigh_' + side], J['calf_' + side], J['foot_' + side]

        def at(z, th=th, kn=kn, ft=ft):
            """Point on the leg centre line at height z (hip -> knee -> ankle)."""
            a, b = (th, kn) if z >= kn.z else (kn, ft)
            p = a.lerp(b, (a.z - z) / (a.z - b.z))
            return Vector((p.x, p.y, z))
        lv = [(0.93, 0.106, 0.110), (0.84, 0.106, 0.112), (0.70, 0.096, 0.102), (0.56, 0.081, 0.086),
              (0.48, 0.077, 0.082), (0.40, 0.072, 0.078), (0.30, 0.066, 0.071)]
        pts = [at(z) for z, _, _ in lv]
        loft(bm, pts, [(a, b) for _, a, b in lv], P8(0.6, 0.6, 0.5, 0.5), Vector((0, 1, 0)))
        # cargo-style thigh pocket
        p = at(0.68); box(bm, (p.x + 0.09 * s, p.y - 0.01, 0.68), (0.014, 0.075, 0.09))
        # leggings / gaiters
        gl = [(0.055, 0.068, 0.076), (0.13, 0.064, 0.071), (0.22, 0.066, 0.072), (0.31, 0.072, 0.077), (0.335, 0.077, 0.082)]
        loft(bm, [at(z) for z, _, _ in gl], [(a, b) for _, a, b in gl], P8(0.6, 0.6, 0.5, 0.5), Vector((0, 1, 0)), mi=1)
        for zz in (0.12, 0.2, 0.28):   # gaiter strap ridges
            p = at(zz); box(bm, (p.x + 0.069 * s, p.y - 0.01, zz), (0.008, 0.03, 0.012), 1)
    return make_obj("OUT_Trousers", bm, [M['trousers'], M['gaiters']], coll)


def build_boots(J, M, coll):
    bm = bmesh.new()
    for side, s in (('l', 1), ('r', -1)):
        x = 0.115 * s
        rings = [(0.078, 0.046, 0.044, 0.046), (0.045, 0.054, 0.052, 0.054), (-0.03, 0.046, 0.056, 0.046),
                 (-0.11, 0.037, 0.055, 0.037), (-0.165, 0.031, 0.046, 0.031), (-0.19, 0.027, 0.030, 0.023)]
        pts = [Vector((x, y, cz)) for y, cz, _, _ in rings]
        loft(bm, pts, [(hx, hy) for _, _, hx, hy in rings], P8(0.7, 0.7, 0.5, 0.5), Vector((0, 0, 1)))
        vloft(bm, [(0.03, 0.058, 0.064, 0.012, x), (0.11, 0.06, 0.066, 0.012, x)], P8(0.6, 0.6, 0.5, 0.5))
        box(bm, (x, -0.055, 0.006), (0.104, 0.262, 0.012), 1)                      # sole
        box(bm, (x, 0.052, 0.014), (0.086, 0.054, 0.026), 1)                       # heel
    return make_obj("OUT_Boots", bm, [M['boots'], M['sole']], coll)


def build_helmet(J, M, coll):
    bm = bmesh.new()
    # deep dome with a modest flared rim
    vloft(bm, [(1.722, 0.112, 0.120, -0.004), (1.708, 0.137, 0.146, -0.004), (1.717, 0.139, 0.148, -0.004),
               (1.730, 0.124, 0.133, -0.004), (1.785, 0.122, 0.131, -0.002), (1.834, 0.102, 0.110, 0.0),
               (1.862, 0.062, 0.068, 0.0)], ROUND10)
    for s in (1, -1):   # chin strap hanging loose in front of the ears
        box(bm, (0.112 * s, -0.035, 1.672), (0.006, 0.014, 0.075), 1)
    return make_obj("EQ_Helmet", bm, [M['helmet'], M['webbing']], coll)


def build_webbing(J, M, coll):
    bm = bmesh.new()
    hx, hy, yo = torso_at(1.042)
    vloft(bm, [(1.018, hx + 0.012, hy + 0.012, yo), (1.066, hx + 0.012, hy + 0.012, yo)], TORSO_P)   # belt
    fy = yo - hy - 0.012
    box(bm, (0, fy - 0.004, 1.042), (0.034, 0.008, 0.04), 1)                          # buckle
    for x in (0.046, 0.094):
        for s in (1, -1):
            box(bm, (x * s, fy - 0.017, 1.034), (0.043, 0.034, 0.056))                 # ammo pouches
            box(bm, (x * s, fy - 0.019, 1.064), (0.046, 0.038, 0.012))                 # pouch flaps
    # suspenders: front -> over shoulder -> back
    for s in (1, -1):
        x = 0.088 * s
        pts = [Vector((x, front_y(1.07) - 0.014, 1.07)), Vector((x, front_y(1.31) - 0.012, 1.31)),
               Vector((x, front_y(1.42) - 0.008, 1.42)), Vector((x * 1.02, -0.06, 1.497)), Vector((x * 1.02, 0.0, 1.505)),
               Vector((x * 1.02, 0.07, 1.495)), Vector((x, back_y(1.42) + 0.008, 1.42)),
               Vector((x, back_y(1.31) + 0.012, 1.31)), Vector((x, back_y(1.07) + 0.014, 1.07))]
        loft(bm, pts, [(0.004, 0.0125)] * len(pts), SQ, Vector((1, 0, 0)))
    # canteen on the right hip
    vloft(bm, [(0.90, 0.028, 0.046, 0.06, -0.226), (1.0, 0.028, 0.046, 0.06, -0.226)], P8(0.6, 0.6, 0.5, 0.5))
    vloft(bm, [(1.0, 0.012, 0.012, 0.06, -0.226), (1.02, 0.012, 0.012, 0.06, -0.226)], SQ, mi=1)
    return make_obj("EQ_Webbing", bm, [M['webbing'], M['metal']], coll)


def build_props(M, coll):
    bm = bmesh.new()   # generic bolt-action rifle, grip at origin, barrel -Y, top +Z
    box(bm, (0, 0.20, -0.025), (0.04, 0.28, 0.10)); box(bm, (0, 0.0, 0.0), (0.034, 0.12, 0.045))
    box(bm, (0, -0.33, 0.022), (0.044, 0.48, 0.048)); box(bm, (0, -0.09, 0.035), (0.036, 0.18, 0.042), 1)
    loft(bm, [Vector((0, -0.11, 0.047)), Vector((0, -0.83, 0.047))], [(0.009, 0.009)] * 2, P8(0.6, 0.6, 0.6, 0.6),
         Vector((0, 0, 1)), mi=1)
    box(bm, (0.03, -0.03, 0.05), (0.04, 0.012, 0.012), 1); box(bm, (0, -0.055, -0.03), (0.01, 0.06, 0.03), 1)
    rifle = make_obj("PROP_Rifle", bm, [M['wood'], M['metal']], coll)
    bm = bmesh.new()   # generic frag grenade
    vloft(bm, [(-0.036, 0.016, 0.016, 0), (-0.026, 0.028, 0.028, 0), (0.02, 0.029, 0.029, 0), (0.034, 0.016, 0.016, 0)],
          P8(0.6, 0.6, 0.6, 0.6))
    vloft(bm, [(0.034, 0.01, 0.01, 0), (0.05, 0.01, 0.01, 0)], SQ, mi=1)
    box(bm, (0.016, 0, 0.02), (0.006, 0.012, 0.05), 1)
    gren = make_obj("PROP_Grenade", bm, [M['grenade'], M['metal']], coll)
    return rifle, gren


# --------------------------------------------------------------------------
# Rig + skinning
# --------------------------------------------------------------------------
FINGERS = ('thumb', 'index', 'middle', 'ring', 'pinky')


def build_rig(J, coll):
    arm = bpy.data.armatures.new("SK_Soldier_Skeleton"); arm.display_type = 'STICK'
    rig = bpy.data.objects.new("SK_Soldier", arm); coll.objects.link(rig); rig.show_in_front = True
    for o in bpy.context.selected_objects: o.select_set(False)
    bpy.context.view_layer.objects.active = rig; rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    eb = arm.edit_bones
    FWD = Vector((0, -1, 0)); UPZ = Vector((0, 0, 1))

    def B(nm, h, t, parent=None, connect=False, roll=FWD, deform=True):
        b = eb.new(nm); b.head = Vector(h); b.tail = Vector(t)
        if parent: b.parent = eb[parent]; b.use_connect = connect
        b.align_roll(roll); b.use_deform = deform

    B("root", (0, 0, 0), (0, 0.25, 0), roll=UPZ, deform=False)
    B("pelvis", J['pelvis'], J['spine_01'], "root")
    B("spine_01", J['spine_01'], J['spine_02'], "pelvis", True)
    B("spine_02", J['spine_02'], J['spine_03'], "spine_01", True)
    B("spine_03", J['spine_03'], J['neck_01'], "spine_02", True)
    B("neck_01", J['neck_01'], J['head'], "spine_03", True)
    B("head", J['head'], J['head_end'], "neck_01", True)
    for side in 'lr':
        back = -J['n_' + side]; d = J['d_' + side]
        B("clavicle_" + side, J['clavicle_' + side], J['upperarm_' + side], "spine_03", roll=UPZ)
        B("upperarm_" + side, J['upperarm_' + side], J['lowerarm_' + side], "clavicle_" + side, True, roll=back)
        B("lowerarm_" + side, J['lowerarm_' + side], J['hand_' + side], "upperarm_" + side, True, roll=back)
        B("hand_" + side, J['hand_' + side], J['palm_end_' + side], "lowerarm_" + side, True, roll=back)
        for f in FINGERS:
            par = "hand_" + side
            keys = [f'{f}_01', f'{f}_02', f'{f}_03', f'{f}_tip']
            for i in range(3):
                nm = f"{f}_0{i+1}_{side}"
                B(nm, J[keys[i] + '_' + side], J[keys[i + 1] + '_' + side], par, i > 0, roll=back); par = nm
        g = J['grip_' + side]
        B("weapon_" + side, g, g + 0.08 * d, "hand_" + side, roll=back, deform=False)
        B("thigh_" + side, J['thigh_' + side], J['calf_' + side], "pelvis")
        B("calf_" + side, J['calf_' + side], J['foot_' + side], "thigh_" + side, True)
        B("foot_" + side, J['foot_' + side], J['ball_' + side], "calf_" + side, True, roll=UPZ)
        B("ball_" + side, J['ball_' + side], J['toe_' + side], "foot_" + side, True, roll=UPZ)
    B("ik_foot_root", (0, 0, 0), (0, 0.15, 0), "root", roll=UPZ, deform=False)
    B("ik_hand_root", (0, 0, 0), (0, 0.15, 0), "root", roll=UPZ, deform=False)
    B("ik_hand_gun", J['hand_r'], J['palm_end_r'], "ik_hand_root", roll=-J['n_r'], deform=False)
    for side in 'lr':
        B("ik_foot_" + side, J['foot_' + side], J['ball_' + side], "ik_foot_root", roll=UPZ, deform=False)
        B("ik_hand_" + side, J['hand_' + side], J['palm_end_' + side], "ik_hand_gun", roll=-J['n_' + side], deform=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    return rig


def seg_dist(p, a, b):
    ab = b - a; t = max(0.0, min(1.0, (p - a).dot(ab) / ab.length_squared))
    return (p - (a + ab * t)).length


def attach(ob, rig):
    ob.parent = rig
    m = ob.modifiers.new("Armature", 'ARMATURE'); m.object = rig


def weights_nearest(ob, rig, bones, falloff=2, blend=0.0):
    """Weight each vertex to its nearest bone segment(s) from `bones`."""
    bl = rig.data.bones
    segs = [(nm, bl[nm].head_local, bl[nm].tail_local) for nm in bones]
    groups = {nm: (ob.vertex_groups.get(nm) or ob.vertex_groups.new(name=nm)) for nm in bones}
    for v in ob.data.vertices:
        ds = sorted((seg_dist(v.co, a, b), nm) for nm, a, b in segs)
        if blend > 0 and len(ds) > 1 and ds[1][0] - ds[0][0] < blend:
            w1 = 0.5 + 0.5 * (ds[1][0] - ds[0][0]) / blend
            groups[ds[0][1]].add([v.index], w1, 'REPLACE'); groups[ds[1][1]].add([v.index], 1 - w1, 'REPLACE')
        else:
            groups[ds[0][1]].add([v.index], 1.0, 'REPLACE')


def weights_auto(ob, rig, fallback_bones):
    for o in bpy.context.selected_objects: o.select_set(False)
    ob.select_set(True); rig.select_set(True); bpy.context.view_layer.objects.active = rig
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    unw = [v for v in ob.data.vertices if not any(g.weight > 0.001 for g in v.groups)]
    if unw:
        bl = rig.data.bones
        segs = [(nm, bl[nm].head_local, bl[nm].tail_local) for nm in fallback_bones]
        for v in unw:
            nm = min(segs, key=lambda s: seg_dist(v.co, s[1], s[2]))[0]
            (ob.vertex_groups.get(nm) or ob.vertex_groups.new(name=nm)).add([v.index], 1.0, 'REPLACE')
    for o in bpy.context.selected_objects: o.select_set(False)
    return len(unw)


def weights_transfer(dst, src):
    """Copy skin weights from an outfit mesh onto gear worn over it."""
    for o in bpy.context.selected_objects: o.select_set(False)
    bpy.context.view_layer.objects.active = dst; dst.select_set(True)
    m = dst.modifiers.new("DT", 'DATA_TRANSFER'); m.object = src
    m.use_vert_data = True; m.data_types_verts = {'VGROUP_WEIGHTS'}; m.vert_mapping = 'POLYINTERP_NEAREST'
    m.layers_vgroup_select_src = 'ALL'; m.layers_vgroup_select_dst = 'NAME'
    bpy.ops.object.datalayout_transfer(modifier=m.name)
    bpy.ops.object.modifier_apply(modifier=m.name)
    dst.select_set(False)


def deform_bones(rig, prefixes):
    return [b.name for b in rig.data.bones if b.use_deform and b.name.startswith(prefixes)]


# --------------------------------------------------------------------------
# --------------------------------------------------------------------------
# Faction uniforms: same meshes + skinning, faction-specific materials
# --------------------------------------------------------------------------
FACTION_PARTS = ("OUT_Tunic", "OUT_Trousers", "OUT_Boots", "EQ_Helmet", "EQ_Webbing")
FACTIONS = {  # base material name -> (rgb, roughness, metallic); unlisted materials stay shared
    "Allied": {   # olive drab wool, khaki canvas webbing/leggings, brown leather boots
        "M_Tunic": ((0.17, 0.20, 0.09), 0.9, 0), "M_Trousers": ((0.21, 0.23, 0.12), 0.9, 0),
        "M_Gaiters": ((0.42, 0.39, 0.25), 0.9, 0), "M_Boots": ((0.24, 0.13, 0.07), 0.6, 0),
        "M_Helmet": ((0.14, 0.17, 0.08), 0.7, 0.1), "M_Webbing": ((0.40, 0.37, 0.23), 0.9, 0)},
    "Axis": {     # field grey wool, stone grey trousers, black leather boots/belts, dark grey helmet
        "M_Tunic": ((0.25, 0.26, 0.24), 0.9, 0), "M_Trousers": ((0.30, 0.30, 0.29), 0.9, 0),
        "M_Gaiters": ((0.05, 0.045, 0.04), 0.5, 0), "M_Boots": ((0.05, 0.045, 0.04), 0.5, 0),
        "M_Sole": ((0.03, 0.03, 0.03), 0.8, 0),
        "M_Helmet": ((0.13, 0.14, 0.13), 0.7, 0.1), "M_Webbing": ((0.07, 0.06, 0.05), 0.6, 0)},
}


def build_faction_uniforms(factions=FACTIONS, base_colls=("Outfit_Infantry", "Equipment")):
    """Copy the skinned outfit/equipment meshes into one collection per faction, recoloured.
    Idempotent: safe to re-run on an already-built file."""
    out = {}
    for fac, overrides in factions.items():
        coll = collection("Outfit_" + fac)
        for o in list(coll.objects): bpy.data.objects.remove(o, do_unlink=True)
        mats = {base: material(f"{base}_{fac}", rgb, rough, metal) for base, (rgb, rough, metal) in overrides.items()}
        for part in FACTION_PARTS:
            src, name = bpy.data.objects[part], f"{part}_{fac}"
            if name in bpy.data.meshes: bpy.data.meshes.remove(bpy.data.meshes[name])
            ob = src.copy(); ob.data = src.data.copy(); ob.name = ob.data.name = name   # keeps vgroups, armature mod, parent
            for i, m in enumerate(ob.data.materials):
                if m and m.name in mats: ob.data.materials[i] = mats[m.name]
            coll.objects.link(ob)
        out[fac] = coll
    for n in base_colls:   # neutral template set stays in the file but out of view/render
        c = bpy.data.collections.get(n)
        if c: c.hide_viewport = c.hide_render = True
    return out


def wipe_scene():
    if bpy.context.object and bpy.context.object.mode != 'OBJECT': bpy.ops.object.mode_set(mode='OBJECT')
    for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.armatures, bpy.data.cameras, bpy.data.lights,
                 bpy.data.collections):
        for d in list(coll): coll.remove(d)


def build():
    wipe_scene()
    J = joints()
    M = {k: material("M_" + n, c, r, mt) for k, n, c, r, mt in [
        ('skin', 'Skin', (0.80, 0.58, 0.44), 0.7, 0), ('hair', 'Hair', (0.16, 0.10, 0.06), 0.9, 0),
        ('dark', 'FaceDark', (0.06, 0.04, 0.035), 0.9, 0), ('tunic', 'Tunic', (0.24, 0.27, 0.17), 0.9, 0),
        ('trousers', 'Trousers', (0.36, 0.32, 0.21), 0.9, 0), ('gaiters', 'Gaiters', (0.50, 0.44, 0.30), 0.9, 0),
        ('boots', 'Boots', (0.24, 0.13, 0.07), 0.6, 0), ('sole', 'Sole', (0.07, 0.05, 0.04), 0.8, 0),
        ('helmet', 'Helmet', (0.20, 0.23, 0.15), 0.7, 0.1), ('webbing', 'Webbing', (0.45, 0.42, 0.29), 0.9, 0),
        ('metal', 'Metal', (0.30, 0.30, 0.30), 0.4, 0.9), ('wood', 'Wood', (0.30, 0.15, 0.07), 0.6, 0),
        ('grenade', 'GrenadeOD', (0.20, 0.22, 0.12), 0.6, 0.2)]}
    cR, cB, cO, cE, cP = (collection(n) for n in ("Rig", "Body", "Outfit_Infantry", "Equipment", "Props_Test"))

    head = build_head(J, M, cB); hands = build_hands(J, M, cB)
    tunic = build_tunic(J, M, cO); trousers = build_trousers(J, M, cO); boots = build_boots(J, M, cO)
    helmet = build_helmet(J, M, cE); webbing = build_webbing(J, M, cE)
    rig = build_rig(J, cR)

    # ---- skinning ----
    report = {}
    report['OUT_Tunic'] = weights_auto(tunic, rig, deform_bones(rig, ('spine', 'pelvis', 'clavicle', 'upperarm', 'lowerarm', 'neck')))
    report['OUT_Trousers'] = weights_auto(trousers, rig, deform_bones(rig, ('pelvis', 'thigh', 'calf', 'spine_01')))
    for ob, bones, blend in ((boots, ['calf_l', 'foot_l', 'ball_l', 'calf_r', 'foot_r', 'ball_r'], 0.02),
                             (hands, deform_bones(rig, ('hand', 'thumb', 'index', 'middle', 'ring', 'pinky', 'lowerarm')), 0.006)):
        weights_nearest(ob, rig, bones, blend=blend); attach(ob, rig)
    # head: rigid head, neck blends into spine_03
    for nm in ('head', 'neck_01', 'spine_03'): head.vertex_groups.new(name=nm)
    for v in head.data.vertices:
        z = v.co.z; wh = ss(1.535, 1.575, z); ws = 1 - ss(1.46, 1.50, z); wn = max(0.0, 1 - wh - ws)
        for nm, w in (('head', wh), ('neck_01', wn), ('spine_03', ws)):
            if w > 0.001: head.vertex_groups[nm].add([v.index], w, 'REPLACE')
    attach(head, rig)
    helmet.vertex_groups.new(name='head').add(list(range(len(helmet.data.vertices))), 1.0, 'REPLACE'); attach(helmet, rig)
    weights_transfer(webbing, tunic); attach(webbing, rig)

    rifle, gren = build_props(M, cP)
    for pb in rig.pose.bones: pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()

    def bone_attach(o, bone, mw):
        o.parent = rig; o.parent_type = 'BONE'; o.parent_bone = bone
        pb = rig.pose.bones[bone]
        o.matrix_parent_inverse = (rig.matrix_world @ pb.matrix @ Matrix.Translation((0, pb.length, 0))).inverted()
        o.matrix_world = mw
    up = -J['d_r']
    bone_attach(rifle, "weapon_r", Matrix.Translation(J['grip_r']) @ Matrix.Rotation(math.atan2(up.x, up.z), 4, 'Y'))
    bone_attach(gren, "weapon_l", Matrix.Translation(J['grip_l']))
    build_faction_uniforms()

    # scene dressing
    cam = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera")); bpy.context.scene.collection.objects.link(cam)
    cam.location = (1.7, -3.8, 1.5); cam.rotation_euler = (math.radians(84), 0, math.radians(24)); bpy.context.scene.camera = cam
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", 'SUN')); bpy.context.scene.collection.objects.link(sun)
    sun.rotation_euler = (math.radians(50), 0, math.radians(35)); sun.data.energy = 3

    tris = {o.name: sum(len(p.vertices) - 2 for p in o.data.polygons) for o in bpy.data.objects if o.type == 'MESH'}
    print("unweighted->fallback:", report)
    print("tris:", tris, "character total:", sum(v for k, v in tris.items() if not k.startswith("PROP_")))
    return rig


# --------------------------------------------------------------------------
# FBX export for Unreal (one skeletal mesh per faction, shared skeleton)
# --------------------------------------------------------------------------
FBX_DIR = r"C:\Users\toony\Projects\Unreal\SourceArt\Characters\FBX"
BODY_PARTS = ("CH_Head", "CH_Hands")


def export_fbx(factions=tuple(FACTIONS), folder=FBX_DIR):
    """Write SK_Soldier_<Faction>.fbx: armature + body + that faction's outfit/equipment.

    - The armature object is renamed "Armature" during export so Unreal does not add it
      as an extra root bone above `root`.
    - Non-deform bones (weapon_l/r, ik_*) are kept: they become weapon attach points in UE.
    - Exported from temporary copies scaled x100 (metres -> centimetres) with no FBX unit
      scaling, so every bone, including `root`, imports at scale 1.0 and the character is
      ~186 UU tall. Import with the legacy FBX importer, convert_scene_unit=False.
    """
    import os
    os.makedirs(folder, exist_ok=True)
    rig = bpy.data.objects["SK_Soldier"]
    for pb in rig.pose.bones: pb.matrix_basis = Matrix.Identity(4)
    S = Matrix.Scale(100.0, 4)
    tmp_coll = bpy.data.collections.new("_fbx_export_tmp"); bpy.context.scene.collection.children.link(tmp_coll)
    out, rig_name, temp = [], rig.name, []
    try:
        rig.name = "_SK_Soldier_src"
        for fac in factions:
            src_meshes = [bpy.data.objects[n] for n in BODY_PARTS] + \
                         [o for o in bpy.data.collections["Outfit_" + fac].objects if o.type == 'MESH']
            # rig copy in centimetres
            arm = rig.data.copy(); r = bpy.data.objects.new("Armature", arm); tmp_coll.objects.link(r); temp.append(r)
            for o in bpy.context.selected_objects: o.select_set(False)
            bpy.context.view_layer.objects.active = r; r.select_set(True)
            bpy.ops.object.mode_set(mode='EDIT')
            # read every joint first: connected bones share head/tail, so scaling in place compounds
            orig = {eb.name: (eb.head.copy(), eb.tail.copy(), eb.roll) for eb in arm.edit_bones}
            for eb in arm.edit_bones:
                h, t, roll = orig[eb.name]
                eb.head = S @ h; eb.tail = S @ t; eb.roll = roll
            bpy.ops.object.mode_set(mode='OBJECT')
            meshes = []
            for so in src_meshes:
                me = so.data.copy(); me.transform(S)
                o = bpy.data.objects.new(so.name + "_x", me); tmp_coll.objects.link(o); temp.append(o)
                # weights live in the mesh data (copied); group names live on the object, same order
                for vg in so.vertex_groups: o.vertex_groups.new(name=vg.name)
                o.parent = r
                md = o.modifiers.new("Armature", 'ARMATURE'); md.object = r
                meshes.append(o)
            bpy.context.view_layer.update()
            path = os.path.join(folder, f"SK_Soldier_{fac}.fbx")
            with bpy.context.temp_override(selected_objects=[r] + meshes, active_object=r, object=r):
                bpy.ops.export_scene.fbx(
                    filepath=path, use_selection=True, object_types={'ARMATURE', 'MESH'},
                    # Blender still writes a x100 node scale for a metric scene; 0.01 cancels it
                    apply_unit_scale=False, apply_scale_options='FBX_SCALE_NONE', global_scale=0.01,
                    axis_forward='-Z', axis_up='Y', bake_space_transform=False,
                    mesh_smooth_type='FACE', use_mesh_modifiers=True, use_tspace=False,
                    add_leaf_bones=False, primary_bone_axis='Y', secondary_bone_axis='X',
                    use_armature_deform_only=False, armature_nodetype='NULL', bake_anim=False)
            out.append((path, [o.name for o in src_meshes]))
            for o in list(temp):
                d = o.data; bpy.data.objects.remove(o, do_unlink=True)
                (bpy.data.armatures if isinstance(d, bpy.types.Armature) else bpy.data.meshes).remove(d)
            temp.clear()
    finally:
        for o in temp:
            bpy.data.objects.remove(o, do_unlink=True)
        bpy.data.collections.remove(tmp_coll)
        rig.name = rig_name
    for p, names in out: print("exported", p, names)
    return out


if __name__ == "__main__":
    build()
