"""Procedural builder for the Squad Command base soldier body (Body_Base + eyes).

Run inside Blender with SK_WW2_Soldier_LowPoly.blend open:
    exec(compile(open(PATH).read(), PATH, 'exec'), {'__name__': '__main__'})

Rebuilds Body_Base (body + head + ears) and Eye_L / Eye_R from scratch, then
skins them to the existing SK_Soldier armature. Joint positions here must
match the armature's rest pose. Units: metres (1 m = 100 UU in Unreal).
"""
import bpy, bmesh, math
from mathutils import Vector, Matrix

RIG_NAME = "SK_Soldier"
BODY_NAME = "Body_Base"

# LOW_POLY = True  -> game mesh (~2.5k tris, faceted): no extra torso subdivision,
#                     raw low-poly hands, 16-column head, flat shading.
# LOW_POLY = False -> higher-detail sculpt base (~6.7k tris, smooth).
LOW_POLY = True


# --------------------------------------------------------------------------
# Joints (shared with the armature)
# --------------------------------------------------------------------------
def joints():
    a = math.radians(40)
    J = {}
    J['pelvis'] = Vector((0, 0, 0.94)); J['spine_01'] = Vector((0, 0, 1.06))
    J['spine_02'] = Vector((0, 0, 1.20)); J['spine_03'] = Vector((0, 0, 1.32))
    J['neck_01'] = Vector((0, 0, 1.48)); J['head'] = Vector((0, 0, 1.575)); J['head_end'] = Vector((0, 0, 1.81))
    fingers = {'index': (0.004, -0.026, (0.036, 0.024, 0.02)), 'middle': (0.0, -0.008, (0.040, 0.027, 0.022)),
               'ring': (0.004, 0.010, (0.037, 0.025, 0.02)), 'pinky': (0.012, 0.027, (0.029, 0.019, 0.017))}
    Y = Vector((0, 1, 0))
    for side, s in (('l', 1), ('r', -1)):
        d = Vector((math.cos(a) * s, 0, -math.sin(a))); n = Vector((-math.sin(a) * s, 0, -math.cos(a)))
        sh = Vector((0.19 * s, 0, 1.44)); el = sh + 0.29 * d; el.y = 0.0; wr = el + 0.26 * d; wr.y = -0.01
        J['clavicle_' + side] = Vector((0.03 * s, 0, 1.42)); J['upperarm_' + side] = sh
        J['lowerarm_' + side] = el; J['hand_' + side] = wr
        J['palm_' + side] = wr + 0.05 * d; J['d_' + side] = d; J['n_' + side] = n
        for f, (off, lat, segs) in fingers.items():
            p = wr + (0.092 - off) * d + lat * Y
            cdir = (d + 0.12 * n).normalized(); pts = [p]
            for L in segs:
                pts.append(pts[-1] + L * cdir); cdir = (cdir + 0.15 * n).normalized()
            for i in range(3): J[f'{f}_0{i+1}_{side}'] = pts[i]
            J[f'{f}_tip_{side}'] = pts[3]
        tdir = (0.55 * d - 0.75 * Y + 0.35 * n).normalized()
        pts = [wr + 0.022 * d - 0.018 * Y + 0.012 * n]
        for L in (0.034, 0.030, 0.024):
            pts.append(pts[-1] + L * tdir); tdir = (tdir + 0.1 * n).normalized()
        for i in range(3): J[f'thumb_0{i+1}_{side}'] = pts[i]
        J['thumb_tip_' + side] = pts[3]
        J['thigh_' + side] = Vector((0.10 * s, 0, 0.90)); J['calf_' + side] = Vector((0.11 * s, -0.012, 0.50))
        J['foot_' + side] = Vector((0.115 * s, 0.01, 0.09)); J['ball_' + side] = Vector((0.118 * s, -0.09, 0.035))
        J['toe_' + side] = Vector((0.12 * s, -0.17, 0.035))
    return J


def ss(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def g2(x, z, cx, cz, sx, sz):
    return math.exp(-((x - cx) / sx) ** 2 - ((z - cz) / sz) ** 2)


# --------------------------------------------------------------------------
# Body (skin-modifier skeleton -> subdivided mesh)
# --------------------------------------------------------------------------
def build_body_mesh(J):
    V, R, E = [], [], []

    def add(p, r):
        V.append(tuple(p)); R.append(r); return len(V) - 1

    pel = add((0, 0.008, 0.93), (0.172, 0.145))
    wst = add((0, -0.002, 1.05), (0.13, 0.122))
    rib = add((0, -0.008, 1.17), (0.168, 0.14))
    chs = add((0, -0.016, 1.29), (0.222, 0.155))
    upc = add((0, 0.0, 1.39), (0.20, 0.13))
    E += [(pel, wst), (wst, rib), (rib, chs), (chs, upc)]
    n0 = add((0, 0.0, 1.465), (0.074, 0.07)); n1 = add((0, 0.004, 1.515), (0.062, 0.063))
    n2 = add((0, 0.006, 1.565), (0.056, 0.058))
    E += [(upc, n0), (n0, n1), (n1, n2)]
    for side, s in (('l', 1), ('r', -1)):
        d = J['d_' + side]; sh = J['upperarm_' + side]; el = J['lowerarm_' + side]; wr = J['hand_' + side]
        dl = add(sh + Vector((0.022 * s, 0, -0.035)), (0.086, 0.086))    # deltoid
        bi = add(sh + 0.14 * d, (0.074, 0.076))                          # biceps / triceps
        eb = add(el, (0.053, 0.055))                                     # elbow
        fa = add(el + 0.07 * d, (0.063, 0.057))                          # forearm belly
        wj = add(wr, (0.039, 0.029))                                     # wrist
        pm = add(J['palm_' + side], (0.045, 0.025))
        E += [(upc, dl), (dl, bi), (bi, eb), (eb, fa), (fa, wj), (wj, pm)]
        for f, rr in (('index', 0.0124), ('middle', 0.013), ('ring', 0.0124), ('pinky', 0.0112)):
            prev = pm
            for k in ('01', '02', '03', 'tip'):
                i = add(J[f'{f}_{k}_{side}'], (rr, rr)); E.append((prev, i)); prev = i; rr *= 0.92
        prev = wj; rr = 0.0153
        for k in ('01', '02', '03', 'tip'):
            i = add(J[f'thumb_{k}_{side}'], (rr, rr)); E.append((prev, i)); prev = i; rr *= 0.9
        hip = add(J['thigh_' + side] + Vector((0.008 * s, 0, -0.01)), (0.124, 0.126))
        mt = add(Vector((0.108 * s, -0.016, 0.70)), (0.104, 0.11))        # mid thigh (quads forward)
        kn = add(J['calf_' + side], (0.067, 0.071))                      # knee
        cf = add(Vector((0.112 * s, 0.024, 0.37)), (0.071, 0.088))        # calf belly (pushed back)
        an = add(J['foot_' + side] + Vector((0, 0, 0.01)), (0.046, 0.05))  # ankle
        hl = add(Vector((0.115 * s, 0.045, 0.035)), (0.04, 0.04))       # heel
        bl = add(J['ball_' + side], (0.055, 0.036)); to = add(J['toe_' + side], (0.048, 0.03))
        E += [(pel, hip), (hip, mt), (mt, kn), (kn, cf), (cf, an), (an, hl), (an, bl), (bl, to)]

    me = bpy.data.meshes.new("_skin_tmp"); me.from_pydata(V, E, [])
    ob = bpy.data.objects.new("_skin_tmp", me); bpy.context.scene.collection.objects.link(ob)
    sk = ob.modifiers.new("Skin", 'SKIN'); sk.branch_smoothing = 0.2
    for i, r in enumerate(R): me.skin_vertices[0].data[i].radius = r
    me.skin_vertices[0].data[pel].use_root = True
    ob.modifiers.new("Subd", 'SUBSURF').levels = 1
    dg = bpy.context.evaluated_depsgraph_get()
    bm = bmesh.new(); bm.from_object(ob, dg)
    bpy.data.objects.remove(ob, do_unlink=True); bpy.data.meshes.remove(me)

    if not LOW_POLY:
        # extra resolution on torso + neck for anatomy and the 16-vert neck loop
        faces = [f for f in bm.faces if (abs(f.calc_center_median().x) < 0.215 and 0.70 < f.calc_center_median().z < 1.47)
                 or (f.calc_center_median().z >= 1.45 and abs(f.calc_center_median().x) < 0.1)]
        bmesh.ops.subdivide_edges(bm, edges=list({e for f in faces for e in f.edges}), cuts=1, use_grid_fill=True)
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 4])
    bm.normal_update()

    # anatomy: displace along vertex normals with gaussian bumps
    def bump(c, sig, amt, mirror=True):
        cs = [Vector(c)] + ([Vector((-c[0], c[1], c[2]))] if mirror and c[0] != 0 else [])
        for v in bm.verts:
            if abs(v.co.x) > 0.24 or v.co.z > 1.47: continue
            for cc in cs:
                dd = ((v.co.x - cc.x) / sig[0]) ** 2 + ((v.co.y - cc.y) / sig[1]) ** 2 + ((v.co.z - cc.z) / sig[2]) ** 2
                if dd < 9: v.co += v.normal * amt * math.exp(-dd)
    bump((0.075, -0.12, 1.33), (0.055, 0.06, 0.045), 0.020)      # pectorals
    bump((0.075, -0.12, 1.278), (0.06, 0.06, 0.012), -0.006)     # under-pec line
    bump((0.0, -0.12, 1.30), (0.012, 0.05, 0.07), -0.004)        # sternum groove
    bump((0.0, -0.11, 1.14), (0.05, 0.05, 0.08), 0.009)          # abdomen
    bump((0.0, -0.11, 1.14), (0.007, 0.05, 0.09), -0.004)        # linea alba
    bump((0.035, -0.11, 1.17), (0.03, 0.05, 0.007), -0.0035)     # ab segment line
    bump((0.035, -0.11, 1.105), (0.03, 0.05, 0.007), -0.0035)    # ab segment line
    bump((0.0, -0.10, 1.215), (0.06, 0.05, 0.012), -0.004)       # rib-cage arch
    bump((0.13, -0.09, 1.04), (0.03, 0.06, 0.06), -0.004)        # oblique / iliac line
    bump((0.09, -0.07, 1.428), (0.05, 0.05, 0.009), 0.005)       # collarbones
    bump((0.085, 0.11, 1.32), (0.045, 0.06, 0.06), 0.012)        # shoulder blades
    bump((0.0, 0.11, 1.18), (0.011, 0.06, 0.25), -0.006)         # spine groove
    bump((0.0, 0.11, 1.05), (0.09, 0.05, 0.05), -0.010)          # lumbar curve
    bump((0.072, 0.11, 0.86), (0.065, 0.08, 0.075), 0.040)       # glutes
    bump((0.075, 0.10, 0.80), (0.06, 0.08, 0.01), -0.006)        # gluteal fold
    bump((0.11, -0.06, 0.51), (0.028, 0.04, 0.028), 0.008)       # kneecaps
    bump((0.14, -0.04, 0.68), (0.03, 0.06, 0.08), 0.005)         # quad (vastus lateralis)
    # trapezius: slope the shoulder line up into the neck
    for v in bm.verts:
        ax, z = abs(v.co.x), v.co.z
        if 1.38 < z < 1.50 and ax < 0.23 and abs(v.co.y) < 0.13:
            w = ss(1.38, 1.45, z) * (1 - ss(1.465, 1.50, z)) * ss(-0.07, 0.0, v.co.y)
            v.co.z += w * 0.026 * (1 - ss(0.05, 0.20, ax))
    if LOW_POLY:
        # hands: undo the subdivision so fingers are simple 4-sided boxes
        for side in 'lr':
            d, wr = J['d_' + side], J['hand_' + side]
            hv = [v for v in bm.verts if (v.co - wr).dot(d) > 0.004 and v.co.x * d.x > 0]
            bmesh.ops.unsubdivide(bm, verts=hv, iterations=2)
    bm.normal_update()
    return bm


# --------------------------------------------------------------------------
# Head (custom loops, 32 columns denser at the face)
# --------------------------------------------------------------------------
PROFILE = [  # z, rx, ry (front depth), yc, ryb (back depth)
    (1.555, 0.054, 0.058, 0.006, 0.050), (1.572, 0.057, 0.064, 0.004, 0.052), (1.586, 0.058, 0.072, -0.001, 0.056),
    (1.598, 0.061, 0.081, -0.007, 0.062), (1.608, 0.064, 0.087, -0.008, 0.068), (1.624, 0.067, 0.091, -0.006, 0.078),
    (1.644, 0.071, 0.095, -0.003, 0.090), (1.660, 0.073, 0.097, 0.0, 0.098), (1.684, 0.0765, 0.0995, 0.003, 0.104),
    (1.705, 0.078, 0.100, 0.005, 0.104), (1.724, 0.079, 0.0995, 0.007, 0.100), (1.736, 0.079, 0.098, 0.008, 0.097),
    (1.750, 0.077, 0.094, 0.009, 0.092), (1.765, 0.072, 0.087, 0.010, 0.085), (1.780, 0.063, 0.076, 0.011, 0.074),
    (1.793, 0.050, 0.061, 0.012, 0.059), (1.804, 0.033, 0.041, 0.012, 0.040)]
# SIMPLE_FACE: RTS camera never sees the face -> blocky head with just a brow,
# nose and chin; no eyeballs, lips or brow colours.
SIMPLE_FACE = LOW_POLY

if LOW_POLY:
    ROWS = [1.555, 1.590, 1.620, 1.650, 1.680, 1.715, 1.750, 1.782]
    COLS = [0, 15, 30, 48, 68, 92, 120, 150]            # angular offsets from face centre (one side)
    POLE_Z = 1.806
    EYE_C = (0.034, 1.696); EYE_R = (0.017, 0.010)
    BROW_BAND = None
    EAR_SEG = 4
else:
    ROWS = [1.555, 1.572, 1.586, 1.598, 1.608, 1.617, 1.624, 1.630, 1.636, 1.644, 1.652, 1.660, 1.669,
            1.677, 1.684, 1.690, 1.696, 1.702, 1.708, 1.715, 1.724, 1.736, 1.750, 1.765, 1.780, 1.793, 1.804]
    COLS = [0, 6.5, 13, 19.5, 26, 33, 40, 48, 57, 67, 78, 90, 104, 120, 138, 158]
    POLE_Z = 1.812
    EYE_C = (0.033, 1.696); EYE_R = (0.0145, 0.0080)
    BROW_BAND = None
    EAR_SEG = 12


def prof(z):
    if z <= PROFILE[0][0]: return PROFILE[0][1:]
    for a, b in zip(PROFILE, PROFILE[1:]):
        if z <= b[0]:
            t = (z - a[0]) / (b[0] - a[0]); return tuple(a[i] + (b[i] - a[i]) * t for i in (1, 2, 3, 4))
    return PROFILE[-1][1:]


def nose_profile(z):
    # forward projection on the centre line, and half-width sigma
    if z > 1.708: return 0.0, 0.006
    if z >= 1.661: t = (1.708 - z) / (1.708 - 1.661); return 0.003 + 0.024 * t ** 1.3, 0.0055 + 0.0055 * t
    if z >= 1.649: t = (1.661 - z) / (1.661 - 1.649); return 0.027 * (1 - t) + 0.006 * t, 0.011
    return 0.0, 0.01


def eye_e(ax, z):
    return ((ax - EYE_C[0]) / EYE_R[0]) ** 2 + ((z - EYE_C[1]) / EYE_R[1]) ** 2


def face_forward(x, z):
    ax = abs(x); F = 0.0
    if SIMPLE_FACE:
        F += 0.007 * g2(ax, z, 0.03, 1.715, 0.035, 0.012)    # brow
        F -= 0.006 * g2(ax, z, 0.034, 1.685, 0.02, 0.015)    # eye hollows
        F += 0.022 * g2(x, z, 0.0, 1.66, 0.012, 0.022)       # nose
        F += 0.010 * g2(x, z, 0.0, 1.598, 0.02, 0.015)       # chin
        return F
    F += 0.0065 * g2(ax, z, 0.032, 1.722, 0.028, 0.006)     # brow ridge
    F += 0.002 * g2(x, z, 0.0, 1.715, 0.010, 0.006)         # glabella
    F -= 0.005 * g2(ax, z, 0.034, 1.697, 0.022, 0.013)      # orbit hollow
    e = eye_e(ax, z)
    if e < 1.0: F -= 0.014 * (1.0 - 0.4 * e)                # eye opening (eyeball shows through)
    elif e < 2.6: F += (0.0032 if z > EYE_C[1] else 0.0022) * math.sin(math.pi * (e - 1.0) / 1.6)  # lids
    F -= 0.0025 * g2(ax, z, 0.034, 1.7115, 0.016, 0.0025)   # upper-lid crease
    n, sx = nose_profile(z)
    F += n * math.exp(-(x / sx) ** 2)                        # nose
    F += 0.010 * g2(ax, z, 0.0145, 1.655, 0.0065, 0.0055)   # alae (nostril wings)
    F -= 0.003 * g2(ax, z, 0.022, 1.652, 0.004, 0.006)      # alar crease
    F += 0.0065 * g2(x, z, 0.0, 1.637, 0.020, 0.0045)       # upper lip
    F += 0.0070 * g2(x, z, 0.0, 1.623, 0.017, 0.0045)       # lower lip
    F -= 0.0120 * g2(x, z, 0.0, 1.630, 0.023, 0.0022)       # mouth line
    F -= 0.004 * g2(ax, z, 0.026, 1.630, 0.005, 0.005)      # mouth corners
    F -= 0.0015 * g2(x, z, 0.0, 1.645, 0.004, 0.005)        # philtrum groove
    F -= 0.003 * g2(x, z, 0.0, 1.612, 0.015, 0.004)         # labiomental groove
    F += 0.013 * g2(x, z, 0.0, 1.598, 0.018, 0.009)         # chin
    F += 0.004 * g2(ax, z, 0.016, 1.599, 0.008, 0.007)      # chin corners (square jaw)
    F += 0.006 * g2(ax, z, 0.052, 1.676, 0.014, 0.011)      # cheekbones
    F -= 0.003 * g2(ax, z, 0.050, 1.638, 0.014, 0.012)      # cheek hollows
    F -= 0.0025 * g2(ax, z, 0.030, 1.643, 0.004, 0.012)     # nasolabial fold
    return F


def head_point(phi_deg, z):
    """phi = angular offset from the face centre (0 = front, +-180 = back)."""
    rx, ry, yc, ryb = prof(z)
    th = math.radians(-90 + phi_deg)
    c, s = math.cos(th), math.sin(th)
    p = 2.5 if s < 0 else 2.0                                  # flatter face front
    x = rx * math.copysign(abs(c) ** (2 / p), c)
    y = yc + (ry if s < 0 else ryb) * math.copysign(abs(s) ** (2 / p), s)
    front = ss(0.25, 0.65, -s)
    y -= face_forward(x, z) * front
    return Vector((x, y, z))


def add_head(bm):
    NC = 2 * len(COLS)                     # head columns; neck loop has NC/2 verts
    step = 360.0 / NC
    U = [-180 + step * j for j in range(NC)]
    N = [-180] + sorted([-o for o in COLS[1:]]) + COLS
    assert len(N) == NC and N[NC // 2] == 0
    cand = [v for v in bm.verts if v.is_boundary and v.co.z > 1.45]
    assert len(cand) == NC // 2, f"neck loop has {len(cand)} verts, expected {NC // 2}"
    loop = [None] * NC
    for v in cand:
        off = (math.degrees(math.atan2(v.co.y, v.co.x)) + 90 + 180) % 360
        j = (2 * int(round(off / (2 * step)))) % NC
        loop[j] = v
    neck = [loop[j] for j in range(0, NC, 2)]
    assert all(neck), "neck loop verts could not be matched to head columns"
    rings = []
    for z in ROWS:
        t = ss(1.555, 1.60, z)
        rings.append([bm.verts.new(head_point(U[j] * (1 - t) + N[j] * t, z)) for j in range(NC)])
    r0 = rings[0]
    for k in range(NC // 2):
        b0, b1 = neck[k], neck[(k + 1) % (NC // 2)]
        c0, c1, c2 = r0[2 * k], r0[2 * k + 1], r0[(2 * k + 2) % NC]
        bm.faces.new((b0, b1, c2, c1)); bm.faces.new((b0, c1, c0))
    for ra, rb in zip(rings, rings[1:]):
        for j in range(NC):
            bm.faces.new((ra[j], ra[(j + 1) % NC], rb[(j + 1) % NC], rb[j]))
    pole = bm.verts.new((0, 0.012, POLE_Z))
    for j in range(NC):
        bm.faces.new((rings[-1][j], rings[-1][(j + 1) % NC], pole))


def add_ears(bm):
    for s in (1, -1):
        z0, y0 = 1.672, 0.014
        rx, ry, yc, ryb = prof(z0)
        x0 = rx + 0.001
        R = []; I = []; B = []
        nseg = EAR_SEG
        for i in range(nseg):
            t = 2 * math.pi * i / nseg
            v_, u_ = 0.0165 * math.cos(t), 0.029 * math.sin(t) + (0.003 if math.sin(t) > 0 else 0)
            R.append((v_, u_, 0.009 + 0.002 * max(0, math.sin(t))))       # helix rim
            I.append((0.55 * v_ + 0.002, 0.55 * u_ - 0.003, 0.004))         # concha wall
            B.append((0.85 * v_, 0.85 * u_, -0.006))                         # root inside head
        C = (0.001, -0.004, 0.0)                                             # concha bowl
        rot = Matrix.Rotation(math.radians(18), 3, 'Z')                      # flare backwards
        tilt = Matrix.Rotation(math.radians(-12), 3, 'X')                    # lean back

        def place(p):
            v_, u_, w_ = p
            loc = Vector((w_ * s, v_, u_))
            loc = tilt @ loc
            loc = Matrix.Rotation(math.radians(18) * s, 3, 'Z') @ loc
            return bm.verts.new(Vector((x0 * s, y0, z0)) + loc)
        RV = [place(p) for p in R]; IV = [place(p) for p in I]; BV = [place(p) for p in B]
        CV = place(C); BC = place((0, 0, -0.008))
        for i in range(nseg):
            j = (i + 1) % nseg
            bm.faces.new((RV[i], RV[j], IV[j], IV[i]))
            bm.faces.new((IV[i], IV[j], CV))
            bm.faces.new((BV[i], BV[j], RV[j], RV[i]))
            bm.faces.new((BV[j], BV[i], BC))


# --------------------------------------------------------------------------
def material(name, rgb, rough=0.6, metal=0.0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*rgb, 1); b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal; m.diffuse_color = (*rgb, 1)
    return m


def build():
    J = joints()
    rig = bpy.data.objects[RIG_NAME]
    coll = bpy.data.collections.get("Body") or bpy.context.scene.collection
    for n in (BODY_NAME, "Eye_L", "Eye_R"):
        o = bpy.data.objects.get(n)
        if o:
            m = o.data; bpy.data.objects.remove(o, do_unlink=True)
            if m.users == 0: bpy.data.meshes.remove(m)

    bm = build_body_mesh(J)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z > 1.535 and abs(v.co.x) < 0.12], context='VERTS')
    add_head(bm)
    add_ears(bm)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)

    # materials: 0 skin, 1 undershorts, 2 brows, 3 hair, 4 lips
    mats = [material("M_Skin", (0.62, 0.42, 0.30), 0.55), material("M_Undershorts", (0.55, 0.52, 0.45), 0.8),
            material("M_Brow", (0.10, 0.07, 0.05), 0.8), material("M_Hair", (0.13, 0.09, 0.06), 0.85),
            material("M_Lips", (0.50, 0.29, 0.24), 0.45)]
    HAIR = 3
    if SIMPLE_FACE:
        mats = [mats[0], mats[1], mats[3]]; HAIR = 2
    for f in bm.faces:
        c = f.calc_center_median(); ax = abs(c.x); mi = 0
        head = c.z > 1.55 and ax < 0.095 and len(f.verts) <= 4
        if 0.74 < c.z < 0.99 and ax < 0.25: mi = 1
        if head and c.y < -0.03 and not SIMPLE_FACE:
            browz = 1.7175 + 0.004 * (1 - ((ax - 0.032) / 0.026) ** 2)
            if BROW_BAND:
                if 0.013 < ax < 0.056 and BROW_BAND[0] < c.z < BROW_BAND[1]: mi = 2
            elif 0.009 < ax < 0.056 and browz - 0.0055 < c.z < browz + 0.0055: mi = 2
            lipw = 0.020 if c.z > 1.630 else 0.016
            if ax < lipw and 1.6195 < c.z < 1.640: mi = 4
        if head and ax < 0.088 and c.z > 1.6:
            # hairline height as a function of angle from the face centre (short WW2 cut)
            phi = abs(math.degrees(math.atan2(c.x, -(c.y - 0.004))))
            hl = 1.752 if phi < 35 else (1.752 - (phi - 35) / 35 * 0.035 if phi < 70 else
                 (1.717 - (phi - 70) / 25 * 0.004 if phi < 95 else 1.713 - (phi - 95) / 45 * 0.07 if phi < 140 else 1.643))
            if c.z > hl: mi = HAIR
        f.material_index = mi
        f.smooth = not LOW_POLY

    me = bpy.data.meshes.new(BODY_NAME); bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(BODY_NAME, me); coll.objects.link(ob)
    for m in mats: me.materials.append(m)

    # eyes: sit just behind the lid rim (skipped for the simple face)
    eye_centres = {}
    er = 0.0125
    if not SIMPLE_FACE:
        rim = [v.co for v in me.vertices if v.co.z > 1.67 and v.co.y < -0.04 and 1.05 < eye_e(abs(v.co.x), v.co.z) < 2.0]
        rim_y = sum(c.y for c in rim) / len(rim)
        mw = material("M_EyeWhite", (0.85, 0.83, 0.80), 0.3); mi_ = material("M_EyeIris", (0.08, 0.06, 0.04), 0.3)
    for side, s in (() if SIMPLE_FACE else (('l', 1), ('r', -1))):
        c = Vector((EYE_C[0] * s, rim_y + 0.0045 + er, EYE_C[1]))
        eye_centres[side] = c
        ebm = bmesh.new()
        bmesh.ops.create_uvsphere(ebm, u_segments=8 if LOW_POLY else 12, v_segments=6 if LOW_POLY else 8, radius=er)
        bmesh.ops.rotate(ebm, verts=ebm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(90), 3, 'X'))
        bmesh.ops.translate(ebm, vec=c, verts=ebm.verts)
        em = bpy.data.meshes.new("Eye_" + side.upper()); ebm.to_mesh(em); ebm.free()
        em.materials.append(mw); em.materials.append(mi_)
        for p in em.polygons:
            p.use_smooth = True
            p.material_index = 1 if p.center.y < c.y - er * 0.9 else 0
        eo = bpy.data.objects.new("Eye_" + side.upper(), em); coll.objects.link(eo)

    # move eye bones to the new eyeball centres
    for o in bpy.context.selected_objects: o.select_set(False)
    bpy.context.view_layer.objects.active = rig; rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for side, c in eye_centres.items():
        b = rig.data.edit_bones["eye_" + side]; b.head = c; b.tail = c + Vector((0, -0.025, 0))
        b.align_roll(Vector((0, 0, 1)))
    bpy.ops.object.mode_set(mode='OBJECT')
    for pb in rig.pose.bones: pb.matrix_basis = Matrix.Identity(4)

    # skin: auto weights for the body (jaw / eyes excluded), then hand-set jaw
    for n in ("jaw", "eye_l", "eye_r"): rig.data.bones[n].use_deform = False
    rig.select_set(False); ob.select_set(True); rig.select_set(True); bpy.context.view_layer.objects.active = rig
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    for n in ("jaw", "eye_l", "eye_r"): rig.data.bones[n].use_deform = True
    jaw = ob.vertex_groups.get("jaw") or ob.vertex_groups.new(name="jaw")
    for v in me.vertices:
        z, y, ax = v.co.z, v.co.y, abs(v.co.x)
        if not (1.56 < z < 1.64 and y < 0.03 and ax < 0.085): continue
        w = ss(1.6335, 1.6255, z) * ss(0.03, -0.035, y) * ss(1.562, 1.585, z)
        if w <= 0.01: continue
        for gr in v.groups: gr.weight *= (1 - w)
        jaw.add([v.index], w, 'REPLACE')
    for side in eye_centres:
        eo = bpy.data.objects["Eye_" + side.upper()]
        vg = eo.vertex_groups.new(name="eye_" + side); vg.add(list(range(len(eo.data.vertices))), 1.0, 'REPLACE')
        eo.parent = rig; md = eo.modifiers.new("Armature", 'ARMATURE'); md.object = rig
    for o in bpy.context.selected_objects: o.select_set(False)

    unweighted = sum(1 for v in me.vertices if not any(gr.weight > 0.001 for gr in v.groups))
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    print(f"Body_Base: {len(me.vertices)} verts, {tris} tris, unweighted={unweighted}")
    return ob


if __name__ == "__main__":
    build()
