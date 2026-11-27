"""Squad Command - procedural hand-keyed animation set for the stylized soldier rig.

Run headless (opens the rigged soldier, writes FBX clips + a preview sheet + calibration JSON):
    blender -b SourceArt/Characters/SK_WW2_Soldier_Stylized.blend --python SourceArt/Characters/Tools/build_soldier_anims.py
    optional args after "--":  --only A_Stand_Idle,A_Throw_Stand   --preview-only   --no-export

How it works
  * Every clip is a function t -> pose. A pose is a dict of high-level controls in the CHARACTER frame
    (f = forward, r = right, u = up, metres; the character stands at the origin facing forward = Blender -Y):
        pelvis (f,r,u) + pelvis_rot (pitch fwd, roll right, yaw left, degrees, world)
        sp1/sp2/sp3/neck/head/clav_l/clav_r: (pitch, roll, yaw) deltas in the parent's frame
        gun (f,r,u, yaw_left, pitch_up, roll): the rifle's grip/trigger point and aim
        hand_r / hand_l: 'gun' (grip the rifle) or (f,r,u, fingers dir xyz, back-of-hand dir xyz)
        elbow_*/knee_*: IK pole points; foot_l/foot_r: (f,r,u ankle, pitch(+ = toes down), roll, yaw)
        curl_l/curl_r: finger curl 0..1;  gren: grenade in right hand (preview only; UE shows/hides it)
  * Bones are solved analytically (FK spine, two-bone IK arms/legs) and keyed per frame, so no
    constraints or nla.bake are involved.
  * The rifle rides on the non-deforming `ik_hand_gun` bone: each frame that bone is placed so a mesh
    attached to it with a fixed offset (the "gun socket", written to soldier_anim_calibration.json)
    lands where the pose put the gun. That lets the rifle change hands (grenade throw, drag, death).
  * Export: x100 armature copy named "Armature", FBX_SCALE_NONE + global_scale 0.01 (see
    build_soldier_stylized.export_fbx), one FBX per clip in SourceArt/Characters/FBX/Anims.
"""
import bpy, math, json, os, sys
from mathutils import Vector, Matrix, Quaternion

HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in dir() else \
    r"C:\Users\toony\Projects\Unreal\SourceArt\Characters\Tools"
ROOT = os.path.dirname(HERE)                                  # SourceArt/Characters
FBX_DIR = os.path.join(ROOT, "FBX", "Anims")
CALIB_PATH = os.path.join(FBX_DIR, "soldier_anim_calibration.json")
WEAPONS_BLEND = os.path.join(os.path.dirname(ROOT), "Weapons", "WW2_Weapons_LowPoly.blend")
ANIM_BLEND = os.path.join(ROOT, "SK_WW2_Soldier_Anims.blend")
PREVIEW_DIR = os.environ.get("SOLDIER_PREVIEW_DIR", os.path.join(ROOT, "Previews"))
FPS = 30

# ------------------------------------------------------------------ frame helpers
def C(f, r, u):
    """Character frame (forward, right, up) -> Blender armature space (character faces -Y)."""
    return Vector((-r, -f, u))


def Rx(d): return Matrix.Rotation(math.radians(d), 3, 'X')
def Ry(d): return Matrix.Rotation(math.radians(d), 3, 'Y')
def Rz(d): return Matrix.Rotation(math.radians(d), 3, 'Z')


def E(p=0.0, r=0.0, y=0.0):
    """pitch forward, roll right, yaw left (degrees) in the character frame."""
    return Rz(y) @ Rx(p) @ Ry(-r)


def frame_from(xdir, zhint):
    x = xdir.normalized(); z = (zhint - x * zhint.dot(x)).normalized(); y = z.cross(x)
    return Matrix((x, y, z)).transposed()


def hand_frame(fingers, back):
    """Bone axes for a hand: Y = wrist->fingers, Z = back of the hand, X = Y x Z."""
    y = fingers.normalized(); z = (back - y * back.dot(y)).normalized(); x = y.cross(z)
    return Matrix((x, y, z)).transposed()


def gun_dir(yaw, pitch):
    cy, sy, cp, sp = math.cos(math.radians(yaw)), math.sin(math.radians(yaw)), math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    return C(cp * cy, -cp * sy, sp)


def gun_matrix(g):
    f, r, u, yaw, pitch, roll = g
    x = gun_dir(yaw, pitch)
    rot = frame_from(x, Vector((0, 0, 1)) if abs(x.z) < 0.97 else C(1, 0, 0))
    rot = Matrix.Rotation(math.radians(roll), 3, x) @ rot          # roll + = clockwise seen from behind
    m = rot.to_4x4(); m.translation = C(f, r, u)
    return m


def smooth(x): x = min(max(x, 0.0), 1.0); return x * x * (3 - 2 * x)


# Hand-on-rifle grips, in the rifle frame (X muzzle, Y rifle-left, Z up; origin = trigger/grip).
GRIP_R_POS = Vector((-0.085, -0.030, -0.045))                    # palm centre (weapon_r head) on the pistol grip
GRIP_R_ROT = hand_frame(Vector((0.95, 0.0, -0.30)), Vector((0.0, -1.0, 0.0)))
GRIP_L_POS = Vector((0.215, 0.012, -0.040))                     # palm under the handguard
GRIP_L_ROT = hand_frame(Vector((0.20, -0.97, 0.10)), Vector((0.0, 0.25, -0.97)))
# Grenades in the right hand, relative to weapon_r (bone axes: X across palm index->pinky, Y fingers, Z back).
GREN_MK2_POS = Vector((0.0, 0.012, -0.034)); GREN_MK2_ROT = frame_from(Vector((0, 1, 0)), Vector((-1, 0, 0)))
GREN_STICK_POS = Vector((0.0, 0.012, -0.030)) + Vector((-1, 0, 0)) * 0.06
GREN_STICK_ROT = frame_from(Vector((0, 1, 0)), Vector((-1, 0, 0)))

# ------------------------------------------------------------------ rig data
rig = bpy.data.objects["SK_Soldier"]
arm = rig.data
REST = {b.name: b.matrix_local.copy() for b in arm.bones}
PARENT = {b.name: (b.parent.name if b.parent else None) for b in arm.bones}
ORDER = []
def _walk(b):
    ORDER.append(b.name)
    for c in b.children: _walk(c)
for b in arm.bones:
    if b.parent is None: _walk(b)
LEN = {b.name: b.length for b in arm.bones}
FINGERS = [n for n in ORDER if any(n.startswith(k) for k in ("thumb", "index", "middle", "ring", "pinky"))]


def rot3(m): return m.to_3x3().normalized()


def rest_head(n): return REST[n].translation.copy()


def inherit(n, M):
    p = PARENT[n]
    return M[p] @ REST[p].inverted() @ REST[n]


def with_rot(n, M_inh, A):
    """Bone n placed at its inherited head with world rotation A @ rest rotation."""
    m = (A @ rot3(REST[n])).to_4x4(); m.translation = M_inh.translation
    return m


G0_ROT = rot3(REST["weapon_r"]) @ GRIP_R_ROT.inverted()          # rifle frame in the rest pose, in the right hand
G0 = G0_ROT.to_4x4(); G0.translation = rest_head("weapon_r") - G0_ROT @ GRIP_R_POS
GUN_TO_IKGUN = G0.inverted() @ REST["ik_hand_gun"]               # ik_hand_gun = G @ this
HAND_TO_WEAPON = {s: REST["hand_" + s].inverted() @ REST["weapon_" + s] for s in "lr"}

ARM_FWD = Vector((0, -1, 0)); LEG_BACK = Vector((0, 1, 0))
WARN = []


def two_bone(M, upper, lower, target, pole, bend_hint, A_parent_rot):
    """Analytic IK. upper's head comes from M (already inherited). Writes M[upper], M[lower]; returns end point."""
    S = M[upper].translation
    a, b = LEN[upper], LEN[lower]
    d = target - S; c = d.length
    if c > a + b - 1e-4:
        WARN.append((upper, round(c - (a + b), 3))); c = a + b - 1e-4
    c = max(c, abs(a - b) + 1e-3)
    u = d.normalized()
    v = (pole - S); v = v - u * v.dot(u)
    if v.length < 1e-6: v = bend_hint - u * bend_hint.dot(u)
    v.normalize()
    cos_a = (a * a + c * c - b * b) / (2 * a * c); ang = math.acos(max(-1, min(1, cos_a)))
    Epos = S + a * (math.cos(ang) * u + math.sin(ang) * v)
    W = S + u * c
    d1, d2 = (Epos - S).normalized(), (W - Epos).normalized()
    h1 = d1.cross(d2)
    if h1.length < 1e-5: h1 = d1.cross(v)
    h1.normalize()
    # rest frames (rest hinge from the natural bend direction, rotated by the parent's motion)
    y0u = rot3(REST[upper]).col[1]; y0l = rot3(REST[lower]).col[1]
    h0 = y0u.cross(bend_hint); h0.normalize()
    def fr(y, h):
        h = (h - y * h.dot(y)).normalized(); return Matrix((h, y, h.cross(y))).transposed()
    Qu = fr(d1, h1) @ fr(y0u, h0).inverted()
    Ql = fr(d2, h1) @ fr(y0l, h0).inverted()
    mu = (Qu @ rot3(REST[upper])).to_4x4(); mu.translation = S
    ml = (Ql @ rot3(REST[lower])).to_4x4(); ml.translation = Epos
    M[upper] = mu; M[lower] = ml
    return W


# ------------------------------------------------------------------ pose solve
def solve(P):
    """Pose dict -> (armature-space matrices for every bone, rifle matrix)."""
    M = {}
    M["root"] = REST["root"].copy()
    # pelvis
    A_pel = E(*P["pelvis_rot"])
    if P.get("pelvis_wroll"): A_pel = Ry(-P["pelvis_wroll"]) @ A_pel
    m = (A_pel @ rot3(REST["pelvis"])).to_4x4(); m.translation = C(*P["pelvis"]); M["pelvis"] = m
    acc = {"pelvis": A_pel}

    def fk(n, key):
        A = acc[PARENT[n]]; D = E(*P.get(key, (0, 0, 0)))
        Ab = A @ D; acc[n] = Ab
        M[n] = with_rot(n, inherit(n, M), Ab)

    for n, k in (("spine_01", "sp1"), ("spine_02", "sp2"), ("spine_03", "sp3"), ("neck_01", "neck"),
                 ("head", "head"), ("clavicle_l", "clav_l"), ("clavicle_r", "clav_r")):
        fk(n, k)

    G = gun_matrix(P["gun"])
    gun_rot = rot3(G)

    def hand_target(side):
        h = P["hand_" + side]
        if h == "gun":
            gp, gr = (GRIP_R_POS, GRIP_R_ROT) if side == "r" else (Vector((P.get("grip_l_x", 0.215), GRIP_L_POS.y, GRIP_L_POS.z)), GRIP_L_ROT)
            palm = G @ gp; rot = gun_rot @ gr
        else:
            palm = C(*h[0:3]); rot = hand_frame(C(*h[3:6]), C(*h[6:9]))
        off = HAND_TO_WEAPON[side].translation                     # wrist -> palm, hand-local
        rotb = rot  # bone axes in armature space (weapon_* shares the hand's axes)
        wrist = palm - rotb @ (rot3(REST["hand_" + side]).inverted() @ (rot3(REST["hand_" + side]) @ off))
        return wrist, rotb

    for side in "lr":
        M["upperarm_" + side] = inherit("upperarm_" + side, M)
        wrist, rot = hand_target(side)
        pole = C(*P["elbow_" + side])
        W = two_bone(M, "upperarm_" + side, "lowerarm_" + side, wrist, pole, ARM_FWD, None)
        hm = rot.to_4x4(); hm.translation = W; M["hand_" + side] = hm

    M["thigh_l"] = inherit("thigh_l", M); M["thigh_r"] = inherit("thigh_r", M)
    for side in "lr":
        f = P["foot_" + side]
        W = two_bone(M, "thigh_" + side, "calf_" + side, C(*f[0:3]), C(*P["knee_" + side]), LEG_BACK, None)
        fm = (E(*f[3:6]) @ rot3(REST["foot_" + side])).to_4x4(); fm.translation = W; M["foot_" + side] = fm
        bm = inherit("ball_" + side, M)
        toe = P.get("toe_" + side, 0.0)
        if toe:
            ax = rot3(bm).col[0]; R = Matrix.Rotation(math.radians(toe), 3, ax)
            bm = (R @ rot3(bm)).to_4x4(); bm.translation = inherit("ball_" + side, M).translation
        M["ball_" + side] = bm

    # fingers: inherit, then curl toward the palm
    for n in FINGERS:
        side = n[-1]; mi = inherit(n, M)
        curl = P.get("curl_" + side, 0.0)
        if curl:
            hand = rot3(M["hand_" + side]); palm_dir = -hand.col[2]
            y = rot3(mi).col[1]
            k = 0.55 if n.startswith("thumb") else 1.0
            base = 70 if n.endswith("01_" + side) else 80
            if n.startswith("thumb"): palm_dir = (palm_dir + hand.col[0] * (1 if side == "r" else -1) * -0.6).normalized()
            ax = y.cross(palm_dir)
            if ax.length > 1e-6:
                R = Matrix.Rotation(math.radians(base * curl * k), 3, ax.normalized())
                t = mi.translation; mi = (R @ rot3(mi)).to_4x4(); mi.translation = t
        M[n] = mi
    for side in "lr":
        M["weapon_" + side] = inherit("weapon_" + side, M)
    # IK helper bones (UE convention): feet/hands mirrored, ik_hand_gun carries the rifle
    M["ik_foot_root"] = REST["ik_foot_root"].copy(); M["ik_hand_root"] = REST["ik_hand_root"].copy()
    for side in "lr":
        M["ik_foot_" + side] = M["foot_" + side].copy()
        M["ik_hand_" + side] = M["hand_" + side].copy()
    M["ik_hand_gun"] = G @ GUN_TO_IKGUN
    for n in ORDER:
        if n not in M: M[n] = inherit(n, M)
    if P.get("mirror"):
        return mirror_solved(M, G)
    return M, G


# Left-right mirror (character right = Blender -X): a pose with mirror=1 is solved as authored, then reflected, so the
# rifle goes to the left shoulder with the left hand on the grip (GDD §9.2, leaning out left past a wall's edge).
MIRROR_X = Matrix.Scale(-1.0, 4, Vector((1, 0, 0)))
RIFLE_FLIP_Y = Matrix.Scale(-1.0, 4, Vector((0, 1, 0)))       # keeps the reflected rifle a proper rotation


def mirror_partner(n):
    for a, b in (("_l", "_r"), ("_r", "_l")):
        if n.endswith(a) and n[:-2] + b in REST: return n[:-2] + b
    return n


def mirror_solved(M, G):
    out = {}
    for n in ORDER:
        m = mirror_partner(n)
        # Reflect the partner's world matrix, then undo the reflection in its rest frame: proper, and the rest pose
        # maps to itself on a symmetric skeleton.
        out[n] = MIRROR_X @ M[m] @ (MIRROR_X @ REST[m]).inverted() @ REST[n]
    G2 = MIRROR_X @ G @ RIFLE_FLIP_Y
    out["ik_hand_gun"] = G2 @ GUN_TO_IKGUN
    return out, G2


def basis_of(n, M):
    p = PARENT[n]
    if p is None: return REST[n].inverted() @ M[n]
    return REST[n].inverted() @ REST[p] @ M[p].inverted() @ M[n]


# ------------------------------------------------------------------ pose library
def base(**kw):
    P = dict(pelvis=(0, 0, 0.93), pelvis_rot=(0, 0, 0), sp1=(0, 0, 0), sp2=(0, 0, 0), sp3=(0, 0, 0),
             neck=(0, 0, 0), head=(0, 0, 0), clav_l=(0, 0, 0), clav_r=(0, 0, 0),
             gun=(0.2, 0.12, 1.05, 30, -25, 0), hand_r="gun", hand_l="gun",
             elbow_r=(-0.2, 0.7, 0.9), elbow_l=(0.0, -0.6, 0.8),
             foot_l=(0.08, -0.12, 0.085, 0, 0, 12), foot_r=(-0.08, 0.13, 0.085, 0, 0, -18),
             knee_l=(0.8, -0.2, 0.5), knee_r=(0.8, 0.2, 0.5), toe_l=0.0, toe_r=0.0,
             curl_l=0.85, curl_r=0.85, gren=0, pelvis_wroll=0.0, grip_l_x=0.215)
    P.update(kw); return P


def lerp_pose(A, B, w):
    """Blend two poses. Hands that grip the rifle in both stay on the blended rifle."""
    out = {}
    for k in A:
        a, b = A[k], B[k]
        if k in ("hand_r", "hand_l"):
            if a == "gun" and b == "gun": out[k] = "gun"; continue
            a = a if a != "gun" else resolve_hand(A, k[-1]); b = b if b != "gun" else resolve_hand(B, k[-1])
        if isinstance(a, tuple):
            out[k] = tuple(x + (y - x) * w for x, y in zip(a, b))
        elif isinstance(a, (int, float)):
            out[k] = a + (b - a) * w
        else:
            out[k] = a if w < 0.5 else b
    for k in B:
        if k not in out: out[k] = B[k]
    return out


def resolve_hand(P, side):
    G = gun_matrix(P["gun"])
    gp, gr = (GRIP_R_POS, GRIP_R_ROT) if side == "r" else (Vector((P.get("grip_l_x", 0.215), GRIP_L_POS.y, GRIP_L_POS.z)), GRIP_L_ROT)
    palm = G @ gp; rot = rot3(G) @ gr
    def ch(v): return (-v.y, -v.x, v.z)       # Blender -> character frame
    return ch(palm) + ch(rot.col[1]) + ch(rot.col[2])


def keyed(keys, loop=False):
    """keys: list of (time_seconds, pose). Smoothstep between keys."""
    def fn(t):
        if t <= keys[0][0]: return keys[0][1]
        for (t0, a), (t1, b) in zip(keys, keys[1:]):
            if t <= t1: return lerp_pose(a, b, smooth((t - t0) / (t1 - t0)))
        return keys[-1][1]
    return fn


def add(P, **kw):
    """Copy of pose P with tuple offsets added (e.g. gun=(0,0,-0.4,0,0,0))."""
    Q = dict(P)
    for k, v in kw.items():
        if isinstance(Q.get(k), tuple) and isinstance(v, tuple): Q[k] = tuple(a + b for a, b in zip(Q[k], v))
        else: Q[k] = v
    return Q


def put(P, **kw):
    """Copy of pose P with values replaced."""
    Q = dict(P); Q.update(kw); return Q


def free(f, r, u, fingers, back): return (f, r, u) + tuple(fingers) + tuple(back)


# --- standing
STAND_IDLE = base(pelvis=(0, 0, 0.93), pelvis_rot=(2, 0, -18), sp1=(3, 0, -4), sp2=(3, 0, -4), sp3=(2, 0, 0),
                  neck=(-3, 0, 12), head=(-3, 0, 12), gun=(0.20, 0.10, 1.06, 32, -22, 10),
                  foot_l=(0.10, -0.13, 0.085, 0, 0, 0), foot_r=(-0.10, 0.12, 0.085, 0, 0, -30))
STAND_AIM = base(pelvis=(-0.02, 0, 0.92), pelvis_rot=(3, 0, -32), sp1=(4, 0, -6), sp2=(4, 0, -6), sp3=(3, 0, -4),
                 neck=(-4, 0, 22), head=(-4, 8, 24), clav_r=(0, 0, 8), gun=(0.36, 0.07, 1.47, 0, 0, 0),
                 elbow_r=(-0.1, 0.7, 1.4), elbow_l=(0.3, -0.5, 1.0),
                 foot_l=(0.18, -0.14, 0.085, 0, 0, -10), foot_r=(-0.14, 0.15, 0.085, 0, 0, -45),
                 knee_l=(0.8, -0.2, 0.5), knee_r=(0.6, 0.4, 0.5))
# --- crouched (kneeling on the right knee)
KNEEL_LEGS = dict(foot_l=(0.30, -0.16, 0.085, 0, 0, -5), knee_l=(1.0, -0.25, 0.6),
                  foot_r=(-0.30, 0.16, 0.115, 55, 0, -10), knee_r=(0.6, 0.25, -0.2), toe_r=-40)
CROUCH_IDLE = base(pelvis=(-0.06, 0.02, 0.50), pelvis_rot=(6, 0, -20), sp1=(8, 0, -4), sp2=(6, 0, -4), sp3=(4, 0, 0),
                   neck=(-8, 0, 12), head=(-6, 0, 12), gun=(0.24, 0.10, 0.72, 30, -12, 10), **KNEEL_LEGS)
CROUCH_AIM = base(pelvis=(-0.06, 0.02, 0.50), pelvis_rot=(6, 0, -30), sp1=(6, 0, -6), sp2=(5, 0, -6), sp3=(3, 0, -4),
                  neck=(-6, 0, 22), head=(-4, 8, 24), clav_r=(0, 0, 8), gun=(0.38, 0.08, 1.03, 0, 0, 0),
                  elbow_r=(-0.1, 0.7, 1.0), elbow_l=(0.45, -0.25, 0.55), **KNEEL_LEGS)
# --- prone (lying face down, rifle forward)
PRONE_LEGS = dict(foot_l=(-0.80, -0.26, 0.075, 82, 0, 15), foot_r=(-0.80, 0.26, 0.075, 82, 0, -15),
                  knee_l=(-0.5, -0.4, -0.6), knee_r=(-0.5, 0.4, -0.6))
PRONE_IDLE = base(pelvis=(0, 0, 0.17), pelvis_rot=(88, 0, 0), sp1=(-6, 0, 0), sp2=(-8, 0, 0), sp3=(-8, 0, 0),
                  neck=(-30, 0, 0), head=(-30, 0, 0), gun=(0.88, 0.08, 0.30, 0, 1, 0), grip_l_x=0.08,
                  elbow_r=(0.5, 0.6, -0.6), elbow_l=(0.6, -0.5, -0.6), **PRONE_LEGS)


def breathe(P, amp=1.0, period=3.0):
    def fn(t):
        s = math.sin(2 * math.pi * t / period)
        return add(P, sp3=(1.2 * amp * s, 0, 0), sp2=(0.6 * amp * s, 0, 0), gun=(0, 0, 0.006 * amp * s, 0, 0.6 * amp * s, 0))
    return fn


# --- gaits (in place; the pawn moves itself)
def gait(stance_pose, speed, period, duty, lift, bob, stride_scale=1.0, sway=4.0, gun_bob=0.02, foot_r_off=0.0):
    L = speed * duty * period * stride_scale
    def foot(phase, side, P):
        f0 = P["foot_" + side]
        ph = phase % 1.0
        if ph < duty:
            s = ph / duty; f = L / 2 - L * s; u = f0[2]; pitch = 25 * smooth((s - 0.7) / 0.3) if s > 0.7 else 0
        else:
            s = (ph - duty) / (1 - duty); f = -L / 2 + L * (1 - math.cos(math.pi * s)) / 2
            u = f0[2] + lift * math.sin(math.pi * s); pitch = 25 * (1 - smooth(s / 0.35)) - 15 * smooth((s - 0.6) / 0.4)
        return (f0[0] + f, f0[1], u, f0[3] + pitch, f0[4], f0[5])
    def fn(t):
        ph = (t / period) % 1.0
        P = dict(stance_pose)
        P["foot_l"] = foot(ph, "l", stance_pose); P["foot_r"] = foot(ph + 0.5, "r", stance_pose)
        b = -bob * math.cos(4 * math.pi * ph)
        s = math.sin(2 * math.pi * ph)
        P = add(P, pelvis=(0, 0, b), pelvis_rot=(0, 2 * s, sway * s), sp3=(0, 0, -sway * s * 0.8),
                gun=(0, 0, gun_bob * math.cos(4 * math.pi * ph), 0, 0, 0))
        return P
    return fn


STAND_RUN = base(pelvis=(0.04, 0, 0.88), pelvis_rot=(6, 0, -10), sp1=(4, 0, -4), sp2=(3, 0, -4), sp3=(2, 0, 0),
                 neck=(-6, 0, 9), head=(-5, 0, 9), gun=(0.24, 0.12, 1.18, 42, 24, 20),
                 elbow_r=(-0.1, 0.7, 0.9), elbow_l=(0.1, -0.6, 0.9),
                 foot_l=(0.0, -0.11, 0.085, 0, 0, 0), foot_r=(0.0, 0.11, 0.085, 0, 0, 0),
                 knee_l=(1.0, -0.1, 0.5), knee_r=(1.0, 0.1, 0.5))
CROUCH_RUN = base(pelvis=(0.05, 0, 0.68), pelvis_rot=(16, 0, -10), sp1=(9, 0, -4), sp2=(8, 0, -3), sp3=(6, 0, 0),
                  neck=(-16, 0, 8), head=(-14, 0, 8), gun=(0.36, 0.10, 0.88, 32, -8, 12),
                  elbow_r=(-0.1, 0.7, 0.6), elbow_l=(0.2, -0.6, 0.6),
                  foot_l=(0.02, -0.13, 0.085, 0, 0, 0), foot_r=(0.02, 0.13, 0.085, 0, 0, 0),
                  knee_l=(1.0, -0.15, 0.6), knee_r=(1.0, 0.15, 0.6))
DRAG_POSE = add(CROUCH_RUN, pelvis_rot=(4, 0, 15), sp3=(4, 0, 0), gun=(0, 0, 0, 0, 0, 0),
                hand_r=free(-0.12, 0.30, 0.55, (-0.3, 0.1, -0.9), (-0.9, 0.2, 0.1)), curl_r=1.0)
DRAG_POSE["elbow_r"] = (-0.2, 0.8, 0.9)
DRAG_POSE["gun"] = (0.32, -0.24, 0.80, 0, -45, 85)   # rifle carried in the left hand at the side
DRAG_POSE["elbow_l"] = (-0.1, -0.7, 0.8)


def prone_crawl(speed=0.79, period=1.6):
    duty = 0.55; L = speed * duty * period
    def limb(ph):  # 0 at stance start: forward-most
        ph %= 1.0
        if ph < duty: s = ph / duty; return L / 2 - L * s, 0.0
        s = (ph - duty) / (1 - duty); return -L / 2 + L * (1 - math.cos(math.pi * s)) / 2, math.sin(math.pi * s)
    def fn(t):
        ph = (t / period) % 1.0
        P = dict(PRONE_IDLE)
        fl, ll = limb(ph); fr, lr = limb(ph + 0.5)
        # left forearm pulls; right hand drags the rifle (laid on its side) along the right forearm
        P["hand_l"] = free(0.62 + fl, -0.24, 0.05 + 0.05 * ll, (0.3, 0.6, 0.0), (0.0, 0.0, 1.0))
        P["gun"] = (0.62 + fr, 0.20, 0.10 + 0.05 * lr, -8, 4, 80)
        P["elbow_l"] = (0.3, -0.6, -0.5); P["elbow_r"] = (0.3, 0.6, -0.5)
        P["curl_l"] = 0.3
        # legs: the knee on the side of the forward arm draws up and out
        dl = max(0.0, math.sin(2 * math.pi * (ph + 0.25))); dr = max(0.0, math.sin(2 * math.pi * (ph + 0.75)))
        P["foot_l"] = (-0.80 + 0.28 * dl, -0.26 - 0.18 * dl, 0.075, 82 - 20 * dl, 0, 15 + 25 * dl)
        P["foot_r"] = (-0.80 + 0.28 * dr, 0.26 + 0.18 * dr, 0.075, 82 - 20 * dr, 0, -15 - 25 * dr)
        P["knee_l"] = (-0.3, -0.8, -0.3); P["knee_r"] = (-0.3, 0.8, -0.3)
        s = math.sin(2 * math.pi * ph)
        return add(P, pelvis_rot=(0, 0, 0), pelvis_wroll=5 * s, sp3=(0, 0, 4 * s), head=(0, 0, -3 * s))
    return fn


# --- fire (recoil) and reload
def fire(aim):
    kick = add(aim, gun=(-0.035, 0, 0.012, 0, 6, 0), sp3=(-3, 0, 0), head=(-2, 0, 0))
    return keyed([(0.0, aim), (0.05, kick), (0.40, aim)])


def reload_clip(ready, pouch, receiver_up, prone=False):
    tilt = add(ready, gun=(0.05, -0.04, 0.10 if not prone else 0.02, -25, 20, 35))
    pouch_pose = add(tilt, hand_l=free(*pouch, (0.3, 0.3, -0.9), (0.6, -0.6, 0.3)), curl_l=0.6)
    G = gun_matrix(tilt["gun"]); top = G @ Vector((0.0, 0.0, 0.07 + receiver_up))
    def ch(v): return (-v.y, -v.x, v.z)
    above = ch(G @ Vector((0.0, 0.0, 0.16)))
    insert_hi = add(tilt, hand_l=free(*above, (0.0, 0.3, -0.9), (0.0, -0.5, 0.0)), curl_l=0.5)
    insert = add(tilt, hand_l=free(*ch(top), (0.1, 0.2, -0.95), (0.0, -0.9, 0.3)), curl_l=0.4)
    return keyed([(0.0, ready), (0.25, tilt), (0.55, pouch_pose), (0.70, pouch_pose), (1.05, insert_hi),
                  (1.25, insert), (1.45, insert), (1.75, tilt), (2.0, ready)])


# --- grenade throws: release at exactly 0.5 s (GDD §7 wind-up)
RELEASE = 0.5


def throw_clip(ready, dz=0.0, prone=False, armed=False):
    """armed: the throw after a step back (GDD §7) - starts with the grenade already in hand, pin out (k2)."""
    keys = throw_keys(ready, dz, prone)
    if armed:
        keys = [(0.0, keys[2][1])] + keys[3:]
    return keyed(keys)


def throw_keys(ready, dz=0.0, prone=False):
    def H(f, r, u, fingers, back): return free(f, r, u + dz, fingers, back)
    gun_left = (0.16, -0.24, 1.02 + dz, 15, 55, -70) if not prone else (0.75, -0.18, 0.08, 0, 0, 80)
    left = dict(gun=gun_left, elbow_l=(0.0, -0.8, 0.6 + dz), curl_l=0.9)
    if not prone:
        k1 = put(ready, hand_r=H(0.12, 0.10, 0.95, (0.2, -0.2, -0.9), (0.1, 0.9, 0.2)), curl_r=0.7, gren=0, **left)
        k2 = put(ready, hand_r=H(0.22, 0.05, 1.30, (0.2, -0.2, 0.9), (0.6, 0.5, 0.0)), curl_r=0.9, gren=1, **left)
        k3 = put(ready, pelvis_rot=(-4, -6, -35), sp1=(-4, -3, -10), sp2=(-4, -3, -10), sp3=(-4, -3, -6),
                 neck=(2, 0, 30), head=(2, 0, 30), hand_r=H(-0.30, 0.32, 1.62, (0.0, 0.3, 0.9), (-0.9, 0.2, 0.0)),
                 elbow_r=(-0.4, 0.8, 1.3 + dz), curl_r=0.9, gren=1, **left)
        k4 = put(ready, pelvis_rot=(10, 2, 15), sp1=(8, 2, 8), sp2=(8, 2, 8), sp3=(6, 2, 5),
                 neck=(-8, 0, -12), head=(-6, 0, -12), hand_r=H(0.42, 0.12, 1.68, (0.6, -0.1, 0.8), (0.0, 0.9, -0.2)),
                 elbow_r=(0.0, 0.8, 1.6 + dz), curl_r=0.3, gren=0, **left)
        k5 = put(ready, pelvis_rot=(16, 2, 22), sp1=(10, 2, 10), sp2=(10, 2, 10), sp3=(8, 2, 6),
                 neck=(-12, 0, -18), head=(-8, 0, -14), hand_r=H(0.38, -0.12, 1.00, (0.4, -0.5, -0.7), (0.3, 0.7, 0.2)),
                 elbow_r=(0.2, 0.8, 1.0 + dz), curl_r=0.2, gren=0, **left)
        if dz == 0.0:
            for k in (k3, k4, k5): k["foot_l"] = (0.26, -0.15, 0.085, 0, 0, -10)
            k3["foot_l"] = (0.24, -0.15, 0.085, -12, 0, -10)
        keys = [(0.0, ready), (0.12, k1), (0.30, k2), (0.43, k3), (RELEASE, k4), (0.66, k5), (0.95, put(ready, gren=0)), (1.2, ready)]
    else:
        up = dict(sp1=(-12, 0, 0), sp2=(-14, 0, 0), sp3=(-12, 0, 0), neck=(-20, 0, 0), head=(-20, 0, 0))
        k1 = put(ready, hand_r=free(0.30, 0.25, 0.08, (0.2, 0.3, -0.9), (0.0, 0.9, 0.3)), curl_r=0.7, gren=0, **left, **up)
        k2 = put(ready, hand_r=free(0.25, 0.30, 0.55, (0.2, 0.0, 0.95), (0.0, 0.9, 0.2)), curl_r=0.9, gren=1,
                 pelvis_wroll=-15, **left, **up)
        k3 = put(ready, hand_r=free(0.05, 0.40, 0.75, (0.1, 0.2, 0.95), (-0.9, 0.3, 0.0)), elbow_r=(-0.2, 0.9, 0.9),
                 curl_r=0.9, gren=1, pelvis_wroll=-25, **left, **up)
        k4 = put(ready, hand_r=free(0.75, 0.15, 0.70, (0.8, 0.0, 0.5), (0.0, 0.9, -0.3)), elbow_r=(0.3, 0.9, 0.9),
                 curl_r=0.3, gren=0, pelvis_wroll=-8, **left, **up)
        k5 = put(ready, hand_r=free(0.70, 0.10, 0.10, (0.6, -0.3, -0.6), (0.0, 0.9, 0.3)), curl_r=0.2, gren=0, **left)
        keys = [(0.0, ready), (0.12, k1), (0.30, k2), (0.43, k3), (RELEASE, k4), (0.70, k5), (1.0, put(ready, gren=0)), (1.3, ready)]
    return keys


# --- grenade step back (GDD §7): one cell (1 m) back while the right hand takes the grenade from the pouch and
# pulls the pin against the left hand (which has taken the rifle). In place: the pawn moves itself. Authored over
# 1 s; the game scrubs it in time with the step, so it ends on the armed throw's first pose (k2).
STEP_BACK = 1.0


def back_step_feet(P, start, travel, s, w, lift=0.10):
    """Feet while the pawn moves STEP_BACK backwards over s = 0..1. A planted foot stays put on the ground (so it
    slides forward in the pawn's frame). Right foot: half the way at s 0-1/3 and the rest at 2/3-1; left foot all
    of it at 1/3-2/3. Each foot's place relative to the body blends from start's to travel's by w (e.g. kneeling ->
    crouch-walking -> kneeling), so a stride never opens wider than about half a metre plus the stance."""
    D = STEP_BACK
    def foot(side, segs):
        a, b = start["foot_" + side], travel["foot_" + side]
        home = tuple(x + (y - x) * w for x, y in zip(a, b))
        moved = 0.0; up = home[2]
        for t0, t1, m0, m1 in segs:
            if s >= t1: moved = m1
            elif s > t0:
                k = (s - t0) / (t1 - t0); moved = m0 + (m1 - m0) * smooth(k); up = home[2] + lift * math.sin(math.pi * k)
                break
            else: break
        return (home[0] + moved + D * s, home[1], up) + home[3:]
    P["foot_r"] = foot("r", [(0.0, 1 / 3, 0.0, -D / 2), (2 / 3, 1.0, -D / 2, -D)])
    P["foot_l"] = foot("l", [(1 / 3, 2 / 3, 0.0, -D)])
    return P


def step_back_clip(ready, travel, dz=0.0, prone=False):
    _, (_, k1), (_, k2) = throw_keys(ready, dz, prone)[:3]
    if prone:
        crawl = prone_crawl()
        crawl_time = STEP_BACK / 0.79                 # crawl time to cover the step at prone speed
        prep = keyed([(0.6, k1), (1.0, k2)])          # crawl back, then the grenade comes out at the end
        def fn(s):
            w_in = smooth(s / 0.15); w_out = smooth((s - 0.6) / 0.35)
            P = lerp_pose(ready, crawl(0.4 - crawl_time * s), w_in)   # playing the crawl backwards
            return lerp_pose(P, prep(s), w_out)
        return fn
    # pin: grenade brought to the left hand (holding the rifle), then pulled away from it (= k2)
    kpin = put(k2, hand_r=free(0.20, -0.08, 1.12 + dz, (0.2, -0.4, 0.85), (0.5, 0.6, 0.1)), curl_l=1.0)
    upper = keyed([(0.0, ready), (0.30, k1), (0.62, kpin), (0.72, kpin), (1.0, k2)])
    def fn(s):
        w = smooth(s / 0.2) * (1 - smooth((s - 0.8) / 0.2))    # legs in their travelling pose mid-step
        P = dict(upper(s))
        for k in ("knee_l", "knee_r", "toe_l", "toe_r"):
            a, b = ready[k], travel[k]
            P[k] = tuple(x + (y - x) * w for x, y in zip(a, b)) if isinstance(a, tuple) else a + (b - a) * w
        du = (travel["pelvis"][2] - ready["pelvis"][2]) * w   # rise off the knee / dip while stepping
        P["pelvis"] = (P["pelvis"][0], P["pelvis"][1], P["pelvis"][2] + du)
        return back_step_feet(P, ready, travel, s, w)
    return fn


# --- stance transitions
def transition(a, mid, b, dur):
    return keyed([(0.0, a), (dur * 0.45, mid), (dur, b)])


KNEEL_DOWN_MID = add(CROUCH_IDLE, pelvis=(0, 0, 0.18), pelvis_rot=(-6, 0, 0), sp1=(14, 0, 0), sp2=(10, 0, 0))
KNEEL_DOWN_MID["foot_r"] = (-0.12, 0.16, 0.10, 30, 0, -10)
PRONE_MID = base(pelvis=(-0.10, 0, 0.42), pelvis_rot=(55, 0, -5), sp1=(10, 0, 0), sp2=(8, 0, 0), sp3=(6, 0, 0),
                 neck=(-25, 0, 0), head=(-20, 0, 0), gun=(0.62, 0.12, 0.40, 0, -10, 0),
                 hand_l=free(0.55, -0.24, 0.05, (0.3, 0.2, -0.3), (0.0, 0.0, 1.0)), curl_l=0.2,
                 elbow_r=(0.3, 0.6, 0.3), elbow_l=(0.3, -0.6, 0.6),
                 foot_l=(-0.55, -0.18, 0.08, 60, 0, 5), foot_r=(-0.55, 0.18, 0.08, 60, 0, -5),
                 knee_l=(0.3, -0.3, -0.5), knee_r=(0.3, 0.3, -0.5))

# --- cover (leaning on cover in the next cell, GDD §9.2). The body steps ~0.4 m toward the cover.
COVER_LOW_STAND = base(pelvis=(0.30, 0, 0.92), pelvis_rot=(10, 0, -12), sp1=(10, 0, -4), sp2=(10, 0, -4), sp3=(6, 0, 0),
                       neck=(-14, 0, 10), head=(-10, 0, 8), gun=(0.62, 0.10, 1.00, 4, 4, 15),
                       elbow_r=(0.3, 0.7, 0.7), elbow_l=(0.6, -0.5, 0.75),
                       foot_l=(0.36, -0.13, 0.085, 0, 0, 0), foot_r=(0.16, 0.14, 0.085, 0, 0, -25))
COVER_LOW_CROUCH = base(pelvis=(0.30, 0.02, 0.50), pelvis_rot=(8, 0, -14), sp1=(6, 0, -4), sp2=(5, 0, -4), sp3=(3, 0, 0),
                        neck=(-6, 0, 10), head=(-4, 0, 8), gun=(0.62, 0.10, 0.84, 2, 6, 10),
                        elbow_r=(0.2, 0.7, 0.5), elbow_l=(0.6, -0.5, 0.55),
                        foot_l=(0.52, -0.16, 0.085, 0, 0, -5), knee_l=(1.3, -0.25, 0.6),
                        foot_r=(-0.02, 0.16, 0.115, 55, 0, -10), knee_r=(0.8, 0.25, -0.2), toe_r=-40)
WALL_HAND = (0.70, -0.20, 1.38, (0.05, 0.0, 1.0), (-1.0, 0.0, 0.05))
COVER_HIGH_STAND = base(pelvis=(0.30, 0, 0.93), pelvis_rot=(4, 0, -6), sp1=(3, 0, -3), sp2=(3, 0, -3), sp3=(2, 0, 0),
                        neck=(-2, 0, 25), head=(0, 0, 20), gun=(0.38, 0.22, 0.95, 5, -60, 0),
                        hand_l=free(*WALL_HAND), curl_l=0.15, elbow_l=(0.3, -0.8, 1.0), elbow_r=(-0.2, 0.7, 0.9),
                        foot_l=(0.38, -0.13, 0.085, 0, 0, 5), foot_r=(0.18, 0.15, 0.085, 0, 0, -20))
COVER_HIGH_CROUCH = add(COVER_LOW_CROUCH, gun=(-0.24, 0.12, -0.10, 0, -62, 0), hand_l=free(0.70, -0.20, 0.95, (0.05, 0.0, 1.0), (-1.0, 0.0, 0.05)),
                        curl_l=0.15, neck=(0, 0, 15), head=(0, 0, 10))
COVER_HIGH_CROUCH["gun"] = (0.42, 0.20, 0.62, 5, -60, 0)


def peek(aim, side):
    """Leaning out past a wall's edge. "R": right shoulder, leaning right. "L": the mirror image (left shoulder)."""
    if side == "L":
        return put(peek(aim, "R"), mirror=1)
    s = 1
    return add(aim, pelvis=(0.25, 0.18 * s, -0.02), pelvis_rot=(0, 6 * s, 0), sp1=(0, 6 * s, 0), sp2=(0, 8 * s, 0),
               sp3=(0, 6 * s, 0), neck=(0, -8 * s, 0), head=(0, -6 * s, 0), gun=(0.25, 0.30 * s, -0.04, 0, 0, 12 * s),
               foot_l=(0.30, 0.10 * s, 0, 0, 0, 0), foot_r=(0.20, 0.14 * s, 0, 0, 0, 0))


# --- downed and dead
STUNNED = base(pelvis=(0, 0, 0.15), pelvis_rot=(90, 0, 0), sp1=(0, 0, 0), sp2=(2, 0, 0), sp3=(2, 0, 0),
               neck=(5, 0, 0), head=(5, 0, 20), gun=(0.60, 0.42, 0.04, -20, 0, 88),
               hand_r=free(0.62, 0.10, 0.22, (-0.2, -0.6, 0.0), (0.0, 0.0, 1.0)), hand_l=free(0.62, -0.10, 0.22, (-0.2, 0.6, 0.0), (0.0, 0.0, 1.0)),
               elbow_r=(0.4, 0.9, 0.1), elbow_l=(0.4, -0.9, 0.1), curl_l=0.4, curl_r=0.4,
               foot_l=(-0.80, -0.24, 0.075, 85, 0, 20), foot_r=(-0.80, 0.26, 0.075, 85, 0, -20),
               knee_l=(-0.5, -0.4, -0.6), knee_r=(-0.5, 0.4, -0.6))
INJURED = add(PRONE_IDLE, sp1=(-2, 0, 0), sp2=(-3, 0, 0), sp3=(-3, 3, 0), neck=(-15, 0, 15), head=(-15, 0, 10),
              hand_l=free(0.10, -0.12, 0.05, (0.2, 0.9, 0.0), (0.0, 0.2, 1.0)), curl_l=0.6, elbow_l=(0.2, -0.7, -0.2))
INJURED["gun"] = (0.70, 0.16, 0.10, -6, 3, 70)

DEAD_BACK = base(pelvis=(-0.35, 0, 0.13), pelvis_rot=(-88, 0, 0), sp1=(-2, 0, 0), sp2=(-2, 0, 0), sp3=(0, 0, 0),
                 neck=(8, 0, 0), head=(10, 0, 35), gun=(0.15, 0.70, 0.035, 70, 0, 90),
                 hand_r=free(-0.62, 0.52, 0.04, (0.0, 0.9, 0.0), (0.0, 0.0, -1.0)), hand_l=free(-0.55, -0.50, 0.04, (0.3, -0.9, 0.0), (0.0, 0.0, -1.0)),
                 curl_r=0.4, curl_l=0.3, elbow_r=(-0.4, 0.8, -0.3), elbow_l=(-0.4, -0.8, -0.3),
                 foot_l=(0.48, -0.20, 0.08, -80, 0, 15), foot_r=(0.42, 0.22, 0.10, -70, 0, -25),
                 knee_l=(0.2, -0.3, 1.0), knee_r=(0.2, 0.3, 1.0))


def death_stand(start):
    buckle = add(start, pelvis=(-0.05, 0, -0.25), pelvis_rot=(-10, 0, 0), sp1=(-8, 0, 0), sp2=(-8, 0, 0), neck=(10, 0, 0),
                 hand_r=free(0.20, 0.35, 1.10, (0.3, 0.3, 0.9), (0.0, 0.9, 0.0)), hand_l=free(0.20, -0.35, 1.05, (0.3, -0.3, 0.9), (0.0, -0.9, 0.0)),
                 curl_r=0.2, curl_l=0.2)
    buckle["gun"] = (0.45, 0.30, 0.70, 30, -40, 40)
    fall = base(pelvis=(-0.25, 0, 0.45), pelvis_rot=(-50, 0, 0), sp1=(-6, 0, 0), sp2=(-6, 0, 0), neck=(10, 0, 0), head=(10, 0, 10),
                gun=(0.40, 0.55, 0.20, 50, -10, 70), hand_r=free(-0.05, 0.50, 0.75, (0.0, 0.9, 0.4), (0.0, 0.0, 1.0)),
                hand_l=free(-0.05, -0.50, 0.70, (0.0, -0.9, 0.4), (0.0, 0.0, 1.0)), curl_r=0.3, curl_l=0.3,
                elbow_r=(-0.4, 0.8, 0.5), elbow_l=(-0.4, -0.8, 0.5),
                foot_l=(0.28, -0.18, 0.085, -20, 0, 10), foot_r=(0.20, 0.20, 0.09, -15, 0, -20),
                knee_l=(1.0, -0.3, 0.8), knee_r=(1.0, 0.3, 0.8))
    land = add(DEAD_BACK, pelvis=(0, 0, 0.03), sp2=(-4, 0, 0), head=(-6, 0, 0))
    return keyed([(0.0, start), (0.22, buckle), (0.48, fall), (0.74, land), (0.86, DEAD_BACK), (1.2, DEAD_BACK)])


DEAD_PRONE = add(PRONE_IDLE, pelvis=(0, 0, -0.02), sp1=(4, 0, 0), sp2=(6, 0, 0), sp3=(6, 0, 0), neck=(20, 0, 0), head=(15, 0, 40),
                 hand_r=free(0.25, 0.55, 0.04, (0.2, 0.9, 0.0), (0.0, 0.0, 1.0)), hand_l=free(0.55, -0.40, 0.04, (0.7, -0.6, 0.0), (0.0, 0.0, 1.0)),
                 curl_r=0.3, curl_l=0.3, elbow_r=(0.2, 0.8, -0.2), elbow_l=(0.4, -0.8, -0.2))
DEAD_PRONE["gun"] = (0.75, 0.40, 0.035, -30, 0, 90)


def death_prone(start):
    return keyed([(0.0, start), (0.25, add(start, sp3=(10, 0, 0), head=(20, 0, 0))), (0.6, DEAD_PRONE), (1.0, DEAD_PRONE)])


# ------------------------------------------------------------------ clip table
def const(P): return lambda t: P


CLIPS = {
    # name: (function, duration seconds, loops)
    "A_Stand_Idle":        (breathe(STAND_IDLE), 3.0, True),
    "A_Stand_Aim":         (breathe(STAND_AIM, 0.5), 3.0, True),
    "A_Stand_Fire":        (fire(STAND_AIM), 0.4, False),
    "A_Stand_Run":         (gait(STAND_RUN, 2.40, 0.70, 0.38, 0.18, 0.035), 0.70, True),
    "A_Stand_Reload":      (reload_clip(STAND_IDLE, (0.14, -0.13, 0.95), 0.0), 2.0, False),
    "A_Crouch_Idle":       (breathe(CROUCH_IDLE), 3.0, True),
    "A_Crouch_Aim":        (breathe(CROUCH_AIM, 0.5), 3.0, True),
    "A_Crouch_Fire":       (fire(CROUCH_AIM), 0.4, False),
    "A_Crouch_Run":        (gait(CROUCH_RUN, 1.58, 0.84, 0.50, 0.14, 0.03), 0.84, True),
    "A_Crouch_Reload":     (reload_clip(CROUCH_IDLE, (0.12, -0.14, 0.52), 0.0), 2.0, False),
    "A_Prone_Idle":        (breathe(PRONE_IDLE, 0.6), 3.0, True),
    "A_Prone_Fire":        (fire(PRONE_IDLE), 0.4, False),
    "A_Prone_Crawl":       (prone_crawl(), 1.6, True),
    "A_Prone_Reload":      (reload_clip(PRONE_IDLE, (0.45, -0.30, 0.08), 0.0, prone=True), 2.0, False),
    "A_Throw_Stand":       (throw_clip(STAND_IDLE), 1.2, False),
    "A_Throw_Crouch":      (throw_clip(CROUCH_IDLE, dz=-0.42), 1.2, False),
    "A_Throw_Prone":       (throw_clip(PRONE_IDLE, prone=True), 1.3, False),
    "A_Throw_StepBack_Stand":  (step_back_clip(STAND_IDLE, STAND_RUN), 1.0, False),
    "A_Throw_StepBack_Crouch": (step_back_clip(CROUCH_IDLE, CROUCH_RUN, dz=-0.42), 1.0, False),
    "A_Throw_StepBack_Prone":  (step_back_clip(PRONE_IDLE, None, prone=True), 1.0, False),
    "A_Throw_Armed_Stand":     (throw_clip(STAND_IDLE, armed=True), 1.2, False),
    "A_Throw_Armed_Crouch":    (throw_clip(CROUCH_IDLE, dz=-0.42, armed=True), 1.2, False),
    "A_Throw_Armed_Prone":     (throw_clip(PRONE_IDLE, prone=True, armed=True), 1.3, False),
    "A_Stand_To_Crouch":   (transition(STAND_IDLE, KNEEL_DOWN_MID, CROUCH_IDLE, 0.5), 0.5, False),
    "A_Crouch_To_Stand":   (transition(CROUCH_IDLE, KNEEL_DOWN_MID, STAND_IDLE, 0.5), 0.5, False),
    "A_Crouch_To_Prone":   (transition(CROUCH_IDLE, PRONE_MID, PRONE_IDLE, 0.7), 0.7, False),
    "A_Prone_To_Crouch":   (transition(PRONE_IDLE, PRONE_MID, CROUCH_IDLE, 0.7), 0.7, False),
    "A_Stand_To_Prone":    (keyed([(0, STAND_IDLE), (0.3, KNEEL_DOWN_MID), (0.55, PRONE_MID), (0.9, PRONE_IDLE)]), 0.9, False),
    "A_Prone_To_Stand":    (keyed([(0, PRONE_IDLE), (0.35, PRONE_MID), (0.6, KNEEL_DOWN_MID), (0.9, STAND_IDLE)]), 0.9, False),
    "A_Cover_Low_Stand":   (breathe(COVER_LOW_STAND), 3.0, True),
    "A_Cover_Low_Crouch":  (breathe(COVER_LOW_CROUCH), 3.0, True),
    "A_Cover_High_Stand":  (breathe(COVER_HIGH_STAND), 3.0, True),
    "A_Cover_High_Crouch": (breathe(COVER_HIGH_CROUCH), 3.0, True),
    "A_Cover_Peek_R_Stand":  (breathe(peek(STAND_AIM, "R"), 0.5), 3.0, True),
    "A_Cover_Peek_L_Stand":  (breathe(peek(STAND_AIM, "L"), 0.5), 3.0, True),
    "A_Cover_Peek_R_Crouch": (breathe(peek(CROUCH_AIM, "R"), 0.5), 3.0, True),
    "A_Cover_Peek_L_Crouch": (breathe(peek(CROUCH_AIM, "L"), 0.5), 3.0, True),
    "A_Cover_Peek_R_Stand_Fire":  (fire(peek(STAND_AIM, "R")), 0.4, False),
    "A_Cover_Peek_L_Stand_Fire":  (fire(peek(STAND_AIM, "L")), 0.4, False),
    "A_Cover_Peek_R_Crouch_Fire": (fire(peek(CROUCH_AIM, "R")), 0.4, False),
    "A_Cover_Peek_L_Crouch_Fire": (fire(peek(CROUCH_AIM, "L")), 0.4, False),
    "A_Stunned":           (lambda t: add(STUNNED, sp3=(1.5 * math.sin(2 * math.pi * 1.5 * t), 0, 0), head=(0, 0, 3 * math.sin(math.pi * t))), 2.0, True),
    "A_Injured_Idle":      (breathe(INJURED, 1.6, 1.5), 3.0, True),
    "A_Injured_Crawl":     (prone_crawl(0.79, 2.0), 2.0, True),
    "A_Drag_Idle":         (breathe(DRAG_POSE), 3.0, True),
    "A_Drag_Move":         (gait(DRAG_POSE, 1.58, 0.84, 0.50, 0.12, 0.03), 0.84, True),
    "A_Death_Stand":       (death_stand(STAND_IDLE), 1.2, False),
    "A_Death_Prone":       (death_prone(PRONE_IDLE), 1.0, False),
}


def frames_of(dur, loop):
    n = max(2, int(round(dur * FPS)))
    return n + 1                         # loops end on a copy of the first frame, so UE length == period


# ------------------------------------------------------------------ export
def make_export_rig():
    S = Matrix.Scale(100.0, 4)
    a = arm.copy(); r = bpy.data.objects.new("Armature", a)
    coll = bpy.data.collections.new("_anim_export"); bpy.context.scene.collection.children.link(coll); coll.objects.link(r)
    for o in bpy.context.selected_objects: o.select_set(False)
    bpy.context.view_layer.objects.active = r; r.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    orig = {eb.name: (eb.head.copy(), eb.tail.copy(), eb.roll) for eb in a.edit_bones}
    for eb in a.edit_bones:
        h, t, roll = orig[eb.name]; eb.head = S @ h; eb.tail = S @ t; eb.roll = roll
    bpy.ops.object.mode_set(mode='OBJECT')
    for pb in r.pose.bones: pb.rotation_mode = 'QUATERNION'
    return r


def key_clip(target, name, fn, n, scale, loop):
    act = bpy.data.actions.new(name)
    target.animation_data_create(); target.animation_data.action = act
    prevq = {}
    for i in range(n):
        t = i / FPS
        M, _ = solve(fn(t))
        for bn in ORDER:
            B = basis_of(bn, M)
            q = B.to_quaternion()
            if bn in prevq and prevq[bn].dot(q) < 0: q.negate()
            prevq[bn] = q
            pb = target.pose.bones[bn]
            pb.rotation_quaternion = q; pb.location = B.translation * scale
            pb.keyframe_insert("rotation_quaternion", frame=i + 1); pb.keyframe_insert("location", frame=i + 1)
    act.use_fake_user = True
    return act


def export_clip(r, name, n):
    sc = bpy.context.scene
    sc.frame_start, sc.frame_end = 1, n
    path = os.path.join(FBX_DIR, name + ".fbx")
    with bpy.context.temp_override(selected_objects=[r], active_object=r, object=r):
        bpy.ops.export_scene.fbx(
            filepath=path, use_selection=True, object_types={'ARMATURE'},
            apply_unit_scale=False, apply_scale_options='FBX_SCALE_NONE', global_scale=0.01,
            axis_forward='-Z', axis_up='Y', bake_space_transform=False, add_leaf_bones=False,
            primary_bone_axis='Y', secondary_bone_axis='X', use_armature_deform_only=False, armature_nodetype='NULL',
            bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False,
            bake_anim_use_all_actions=False, bake_anim_force_startend_keying=True, bake_anim_step=1.0,
            bake_anim_simplify_factor=0.0)
    return path


# ------------------------------------------------------------------ preview
def load_props():
    want = ["SM_M1Garand", "SM_Mk2Grenade"]
    with bpy.data.libraries.load(WEAPONS_BLEND, link=False) as (src, dst):
        dst.objects = [n for n in src.objects if n in want]
    out = {}
    for o in dst.objects:
        if o is None: continue
        bpy.context.scene.collection.objects.link(o); o.parent = None; out[o.name] = o
    for n in ("PROP_Rifle", "PROP_Grenade"):
        if n in bpy.data.objects: bpy.data.objects[n].hide_render = True
    return out


def preview(shots, path, cols=6, res=300):
    import numpy as np
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.light = 'STUDIO'; sc.display.shading.color_type = 'MATERIAL'
    sc.render.resolution_x = sc.render.resolution_y = res; sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = 'PNG'
    sc.render.film_transparent = False
    if "PreviewGround" not in bpy.data.objects:
        bpy.ops.mesh.primitive_plane_add(size=4)
        g = bpy.context.active_object; g.name = "PreviewGround"
        m = bpy.data.materials.new("PreviewGround"); m.diffuse_color = (0.55, 0.55, 0.5, 1); g.data.materials.append(m)
    cam = bpy.data.objects["Camera"]; sc.camera = cam
    props = load_props()
    gun, gren = props.get("SM_M1Garand"), props.get("SM_Mk2Grenade")
    for o in bpy.data.collections["Outfit_Axis"].objects: o.hide_render = True
    for pb in rig.pose.bones: pb.rotation_mode = 'QUATERNION'
    tiles = []
    for label, fn, t, view in shots:
        P = fn(t); M, G = solve(P)
        for bn in ORDER:
            B = basis_of(bn, M); pb = rig.pose.bones[bn]
            pb.rotation_quaternion = B.to_quaternion(); pb.location = B.translation
        if gun: gun.matrix_world = G
        if gren:
            gren.hide_render = not P.get("gren")
            gm = M["weapon_r"] @ (GREN_MK2_ROT.to_4x4() @ Matrix.Translation(Vector()))
            gm = M["weapon_r"].copy(); rel = GREN_MK2_ROT.to_4x4(); rel.translation = GREN_MK2_POS
            gren.matrix_world = M["weapon_r"] @ rel
        az, el, dist, tgt_u = view
        tgt = Vector((0, 0, tgt_u))
        d = Vector((math.sin(math.radians(az)) * math.cos(math.radians(el)), -math.cos(math.radians(az)) * math.cos(math.radians(el)), math.sin(math.radians(el))))
        cam.location = tgt + d * dist
        cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
        cam.data.lens = 40
        f = os.path.join(PREVIEW_DIR, "_tile.png"); sc.render.filepath = f
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(f, check_existing=False)
        px = np.array(img.pixels[:], dtype=np.float32).reshape(res, res, 4)
        bpy.data.images.remove(img)
        tiles.append(px)
    rows = (len(tiles) + cols - 1) // cols
    sheet = np.ones((rows * res, cols * res, 4), dtype=np.float32)
    for i, px in enumerate(tiles):
        r_, c_ = divmod(i, cols)
        y0 = (rows - 1 - r_) * res
        sheet[y0:y0 + res, c_ * res:(c_ + 1) * res] = px
    out = bpy.data.images.new("sheet", cols * res, rows * res, alpha=True)
    out.pixels = sheet.ravel().tolist(); out.filepath_raw = path; out.file_format = 'PNG'; out.save()
    print("preview ->", path, [s[0] for s in shots])


# ------------------------------------------------------------------ calibration (for the UE sockets)
def write_calibration():
    def mat(m): return [list(r) for r in m]
    gun_in_ikgun = REST["ik_hand_gun"].inverted() @ G0
    mk2 = GREN_MK2_ROT.to_4x4(); mk2.translation = GREN_MK2_POS
    stick = GREN_STICK_ROT.to_4x4(); stick.translation = GREN_STICK_POS
    data = {
        "note": "Blender rest-pose armature-space matrices (metres). Object frames: X forward/muzzle, Z up.",
        "bones": {n: mat(REST[n]) for n in ("root", "pelvis", "head", "hand_r", "hand_l", "foot_l", "foot_r", "weapon_r", "ik_hand_gun", "spine_03", "upperarm_l", "upperarm_r")},
        "gun_rest_world": mat(G0),
        "gun_in_ik_hand_gun": mat(gun_in_ikgun),
        "mk2_rest_world": mat(REST["weapon_r"] @ mk2),
        "stick_rest_world": mat(REST["weapon_r"] @ stick),
        "release_time": RELEASE,
        "clips": {k: {"seconds": (frames_of(v[1], v[2]) - 1) / FPS, "loop": v[2]} for k, v in CLIPS.items()},
    }
    with open(CALIB_PATH, "w") as fh: json.dump(data, fh, indent=1)
    print("calibration ->", CALIB_PATH)


# ------------------------------------------------------------------ main
def main(argv):
    only = None; do_export = "--no-export" not in argv and "--preview-only" not in argv
    if "--only" in argv: only = set(argv[argv.index("--only") + 1].split(","))
    os.makedirs(FBX_DIR, exist_ok=True); os.makedirs(PREVIEW_DIR, exist_ok=True)
    bpy.context.scene.render.fps = FPS
    names = [n for n in CLIPS if only is None or n in only]
    if do_export:
        r = make_export_rig()
        for nm in names:
            fn, dur, loop = CLIPS[nm]; n = frames_of(dur, loop)
            WARN.clear()
            key_clip(r, nm, fn, n, 100.0, loop)
            export_clip(r, nm, n)
            reach = sorted(set(w[0] for w in WARN))
            print("clip %-24s frames=%3d loop=%d %s" % (nm, n, loop, ("OVERREACH " + str(reach) + " max %.3f" % max(w[1] for w in WARN)) if WARN else ""))
        write_calibration()
        # keep the clips on the metre rig too, for looking at in Blender
        for pb in rig.pose.bones: pb.rotation_mode = 'QUATERNION'
        for nm in names:
            fn, dur, loop = CLIPS[nm]; key_clip(rig, nm + "_m", fn, frames_of(dur, loop), 1.0, loop)
        rig.animation_data.action = None
        bpy.data.objects.remove(r, do_unlink=True)
        bpy.ops.wm.save_as_mainfile(filepath=ANIM_BLEND, copy=True)
    if "--no-preview" not in argv:
        V = (float(os.environ.get("PV_AZ", 35)), 8, float(os.environ.get("PV_DIST", 3.6)), float(os.environ.get("PV_U", 0.85))); VP = (60, 25, 3.2, 0.4); VS = (90, 5, 3.4, 0.85)
        sheet = os.path.join(PREVIEW_DIR, "soldier_anim_sheet_%s.png" % ("all" if only is None else "sel"))
        shots = []
        for nm in names:
            fn, dur, loop = CLIPS[nm]
            v = VP if any(k in nm for k in ("Prone", "Stunned", "Injured", "Death")) else V
            times = [0.0, 0.33, 0.5, 0.67, 1.0] if "StepBack" in nm else [0.0] if loop and "Run" not in nm and "Crawl" not in nm and "Move" not in nm else [dur * 0.25, dur * 0.75] if loop else [0.12, 0.30, 0.43, 0.5, 0.66] if "Throw" in nm else [dur * 0.5, dur]
            for t in times: shots.append(("%s@%.2f" % (nm, t), fn, t, v))
        preview(shots, sheet)


argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
main(argv)
