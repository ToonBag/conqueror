"""Import hand-keyed soldier clips (build_soldier_anims.py output) into /Game/Characters/Soldier/Anims/Hand.

Run with the editor closed (headless commandlet, so no Persona editor is open during a replace-import):
    UnrealEditor-Cmd.exe Unreal.uproject -run=pythonscript -script="C:/.../Scripts/Art/import_soldier_anims.py" -unattended -nosplash
Clips to import come from the SOLDIER_CLIPS env var (comma-separated A_* names); default: all FBX files.
Uses the legacy FBX importer, matching how the existing clips were imported (see the anim pipeline notes).
"""
import os
import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
FBX_DIR = os.path.join(PROJECT, "SourceArt", "Characters", "FBX", "Anims")
DEST = "/Game/Characters/Soldier/Anims/Hand"
SKELETON = "/Game/Characters/Soldier/SKEL_Soldier"
REFERENCE = DEST + "/A_Stand_Fire"            # an existing clip whose import settings are copied
COPY_PROPS = ("import_uniform_scale", "convert_scene", "convert_scene_unit", "force_front_x_axis",
              "animation_length", "import_bone_tracks", "remove_redundant_keys", "do_not_import_curve_with_zero",
              "use_default_sample_rate", "custom_sample_rate", "snap_to_closest_frame_boundary",
              "import_translation", "import_rotation")


def log(msg):
    unreal.log_warning("[import_soldier_anims] " + msg)


def options(ref_data):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", False)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("import_animations", True)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    ui.set_editor_property("skeleton", unreal.load_asset(SKELETON))
    ad = ui.get_editor_property("anim_sequence_import_data")
    for p in COPY_PROPS:
        try:
            ad.set_editor_property(p, ref_data.get_editor_property(p))
        except Exception as e:  # property missing in this engine version
            log("skip %s (%s)" % (p, e))
    ad.set_editor_property("convert_scene_unit", False)
    return ui


def run():
    names = [n for n in os.environ.get("SOLDIER_CLIPS", "").split(",") if n]
    if not names:
        names = sorted(f[:-4] for f in os.listdir(FBX_DIR) if f.endswith(".fbx"))
    ref = unreal.load_asset(REFERENCE)
    ref_data = ref.get_editor_property("asset_import_data")
    log("reference import data: " + ", ".join("%s=%s" % (p, ref_data.get_editor_property(p)) for p in COPY_PROPS if hasattr(ref_data, p)))

    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    try:
        tasks = []
        for name in names:
            t = unreal.AssetImportTask()
            t.set_editor_property("filename", os.path.join(FBX_DIR, name + ".fbx"))
            t.set_editor_property("destination_path", DEST)
            t.set_editor_property("destination_name", name)
            t.set_editor_property("automated", True)
            t.set_editor_property("replace_existing", True)
            t.set_editor_property("save", True)
            t.set_editor_property("options", options(ref_data))
            tasks.append(t)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    finally:
        unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 1")

    for name in names:
        a = unreal.load_asset("%s/%s" % (DEST, name))
        if not isinstance(a, unreal.AnimSequence):
            log("MISSING %s" % name)
            continue
        log("%s: %.3f s, skeleton %s" % (name, a.get_play_length(), a.get_editor_property("skeleton").get_name()))
    log("done")


run()
