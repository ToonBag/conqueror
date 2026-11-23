"""
Low-poly explosion pieces for AExplosionFX (grenade / C4 detonations).

Run headless:
    "C:\\Program Files\\Blender Foundation\\Blender 5.3\\blender.exe" -b --python build_explosion_fx.py

Writes FBX/<name>.fbx (flat-shaded, one material slot "M_FX") and WW2_ExplosionFX.blend next to this
script. Import with import_explosion_fx_unreal.py. Every piece is 1 m across (UE scale = size in metres)
so the C++ can size it directly:

    SM_FX_Puff    jittered icosphere (80 tris): flash core, fireball and smoke puffs. Centre origin.
    SM_FX_Spike   5-sided bipyramid from z=-0.1 to z=1 (base at the origin): flash streaks along +Z.
    SM_FX_Clod    squashed jittered icosahedron: dirt thrown up by the blast.
    SM_FX_Scorch  flat ragged star on z=0, facing up: scorch mark left on the ground.
"""
import math, os, random
import bpy, bmesh
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
FBX_DIR = os.path.join(HERE, "FBX")
BLEND_PATH = os.path.join(HERE, "WW2_ExplosionFX.blend")


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mat = bpy.data.materials.new("M_FX")
    return mat


def finish(name, bm, mat):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = False
    me.materials.append(mat)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def jitter_radial(bm, rng, amount, squash_z=1.0):
    for v in bm.verts:
        d = v.co.length
        if d > 1e-6:
            v.co *= 1.0 + rng.uniform(-amount, amount)
        v.co.z *= squash_z


def puff(mat):
    rng = random.Random(11)
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=2, radius=0.5)  # Blender 4+: 2 = 80 faces
    jitter_radial(bm, rng, 0.09)
    return finish("SM_FX_Puff", bm, mat)


def spike(mat, sides=5, radius=0.11, back=0.1):
    bm = bmesh.new()
    ring = [bm.verts.new((radius * math.cos(2 * math.pi * i / sides), radius * math.sin(2 * math.pi * i / sides), 0.12))
            for i in range(sides)]
    tip = bm.verts.new((0, 0, 1.0))
    tail = bm.verts.new((0, 0, -back))
    for i in range(sides):
        a, b = ring[i], ring[(i + 1) % sides]
        bm.faces.new((a, b, tip))
        bm.faces.new((b, a, tail))
    return finish("SM_FX_Spike", bm, mat)


def clod(mat):
    rng = random.Random(23)
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=1, radius=0.5)  # icosahedron, 20 faces
    jitter_radial(bm, rng, 0.25, squash_z=0.7)
    return finish("SM_FX_Clod", bm, mat)


def scorch(mat, points=18):
    rng = random.Random(5)
    bm = bmesh.new()
    centre = bm.verts.new((0, 0, 0))
    rim = []
    for i in range(points):
        a = 2 * math.pi * (i + rng.uniform(-0.25, 0.25)) / points
        r = (0.5 if i % 2 == 0 else 0.33) * rng.uniform(0.82, 1.0)
        rim.append(bm.verts.new((r * math.cos(a), r * math.sin(a), 0)))
    for i in range(points):
        bm.faces.new((centre, rim[i], rim[(i + 1) % points]))
    ob = finish("SM_FX_Scorch", bm, mat)
    # One-sided decal: make sure it faces up.
    if ob.data.polygons[0].normal.z < 0:
        bm = bmesh.new(); bm.from_mesh(ob.data)
        bmesh.ops.reverse_faces(bm, faces=bm.faces); bm.to_mesh(ob.data); bm.free()
    return ob


def export(objs):
    os.makedirs(FBX_DIR, exist_ok=True)
    vl = bpy.context.view_layer
    for ob in objs:
        for o in vl.objects:
            o.select_set(False)
        ob.select_set(True)
        vl.objects.active = ob
        path = os.path.join(FBX_DIR, ob.name + ".fbx")
        with bpy.context.temp_override(selected_objects=[ob], active_object=ob, object=ob):
            bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"},
                                     mesh_smooth_type="FACE", apply_unit_scale=True,
                                     bake_space_transform=False, add_leaf_bones=False,
                                     use_mesh_modifiers=True, axis_forward="-Z", axis_up="Y")
        tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
        lo = Vector([min(v.co[k] for v in ob.data.vertices) for k in range(3)])
        hi = Vector([max(v.co[k] for v in ob.data.vertices) for k in range(3)])
        print("%-14s tris %3d  x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f" % (ob.name, tris, lo.x, hi.x, lo.y, hi.y, lo.z, hi.z))


mat = reset()
objs = [puff(mat), spike(mat), clod(mat), scorch(mat)]
export(objs)
bpy.ops.wm.save_as_mainfile(filepath=BLEND_PATH)
