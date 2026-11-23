"""
Import the cover / environment set (built by build_ww2_cover.py) into Unreal.

Run in the full editor, e.g.:
    UnrealEditor.exe Unreal.uproject -ExecutePythonScript="<this file>"
or from the editor's Python console: exec(open(r"<this file>").read())

* Duplicates /Game/Weapons/Materials/M_Weapon_Master to /Game/Environment/Materials/M_Env_Master
  (BaseColor / Metallic / Roughness) and makes one MI_Env_<Key> per palette colour.
* Imports every FBX in ./FBX with the legacy FBX importer (Interchange breaks on these files' extras
  and makes unsaveable transient materials), keeping the UCX_ collision boxes.
* Puts meshes in /Game/Environment/<Category>/ and assigns MI_Env_* by material slot name.
* Writes a report next to this script (import_report.txt).
"""
import json, os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    os.path.join(unreal.Paths.project_dir(), "SourceArt", "Environment")
FBX_DIR = os.path.join(HERE, "FBX")
REPORT = os.path.join(HERE, "import_report.txt")
ROOT = "/Game/Environment"
MAT_DIR = ROOT + "/Materials"
MASTER_SRC = "/Game/Weapons/Materials/M_Weapon_Master"
MASTER = MAT_DIR + "/M_Env_Master"

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
lines = []


def log(s):
    lines.append(s)
    unreal.log(s)


def make_materials(materials):
    eal.make_directory(MAT_DIR)
    if not eal.does_asset_exist(MASTER):
        eal.duplicate_asset(MASTER_SRC, MASTER)
    master = eal.load_asset(MASTER)
    out = {}
    for slot, spec in materials.items():
        key = slot[len("M_Env_"):]
        path = "%s/MI_Env_%s" % (MAT_DIR, key)
        mi = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
            "MI_Env_" + key, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mi, master)
        r, g, b = spec["color"]
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "BaseColor", unreal.LinearColor(r, g, b, 1.0))
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, "Metallic", spec["metallic"])
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, "Roughness", spec["roughness"])
        out[slot] = mi
    return out


def import_options():
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    smd = ui.get_editor_property("static_mesh_import_data")
    smd.set_editor_property("combine_meshes", True)
    smd.set_editor_property("auto_generate_collision", False)
    smd.set_editor_property("one_convex_hull_per_ucx", True)
    smd.set_editor_property("generate_lightmap_u_vs", True)
    smd.set_editor_property("remove_degenerates", True)
    smd.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    return ui


def run():
    with open(os.path.join(FBX_DIR, "manifest.json")) as f:
        manifest = json.load(f)
    mis = make_materials(manifest["materials"])
    log("materials: %d" % len(mis))

    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    try:
        tasks = []
        for name, cat in manifest["meshes"].items():
            t = unreal.AssetImportTask()
            t.set_editor_property("filename", os.path.join(FBX_DIR, name + ".fbx"))
            t.set_editor_property("destination_path", "%s/%s" % (ROOT, cat))
            t.set_editor_property("destination_name", name)
            t.set_editor_property("automated", True)
            t.set_editor_property("replace_existing", True)
            t.set_editor_property("save", False)
            t.set_editor_property("options", import_options())
            tasks.append(t)
        tools.import_asset_tasks(tasks)
    finally:
        unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 1")

    bad = 0
    for name, cat in manifest["meshes"].items():
        path = "%s/%s/%s" % (ROOT, cat, name)
        mesh = eal.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh):
            log("MISSING %s" % path)
            bad += 1
            continue
        missing = []
        for i, sm in enumerate(mesh.get_editor_property("static_materials")):
            slot = str(sm.get_editor_property("material_slot_name"))
            mi = mis.get(slot)
            if mi:
                mesh.set_material(i, mi)
            else:
                missing.append(slot)
        box = mesh.get_bounding_box()
        mn, mx = box.min, box.max
        body = mesh.get_editor_property("body_setup")
        ucx = len(body.get_editor_property("agg_geom").get_editor_property("convex_elems")) if body else 0
        log("%-30s ucx %d  x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f%s" % (
            name, ucx, mn.x, mx.x, mn.y, mx.y, mn.z, mx.z,
            ("  NO MATERIAL FOR " + ",".join(missing)) if missing else ""))
        if missing:
            bad += 1
    eal.save_directory(ROOT, only_if_is_dirty=False, recursive=True)
    log("done, %d problems" % bad)


try:
    run()
except Exception as e:  # keep the report even on failure
    import traceback
    log("FAILED: %s\n%s" % (e, traceback.format_exc()))
finally:
    with open(REPORT, "w") as f:
        f.write("\n".join(lines) + "\n")
