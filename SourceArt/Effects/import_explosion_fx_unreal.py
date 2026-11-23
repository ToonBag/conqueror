"""
Import the low-poly explosion pieces (built by build_explosion_fx.py) and set up their material.

Run in the full editor:
    UnrealEditor.exe Unreal.uproject -ExecCmds="py <this file>"

* Builds /Game/Effects/M_FX_Master: lit, masked with dithered opacity, rough and non-shiny. Every look value
  comes from the component's custom primitive data, so one material serves every piece with no MIDs:
      0 Opacity (dithered), 1 Emissive (glow strength), 2-4 Color (linear RGB)
  The glow is shaded by the flat face normal (brighter on top), so the facets read on the fireball too.
* Imports FBX/SM_FX_*.fbx into /Game/Effects with the legacy FBX importer (flat normals kept).
* Fills the Effects fields of /Game/Data/DA_SoldierVisuals and saves. Writes import_report.txt here.
"""
import os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    os.path.join(unreal.Paths.project_dir(), "SourceArt", "Effects")
FBX_DIR = os.path.join(HERE, "FBX")
REPORT = os.path.join(HERE, "import_report.txt")
ROOT = "/Game/Effects"
MASTER = ROOT + "/M_FX_Master"
VISUALS = "/Game/Data/DA_SoldierVisuals"
MESHES = {
    "SM_FX_Puff": "ExplosionPuffMesh",
    "SM_FX_Spike": "ExplosionSpikeMesh",
    "SM_FX_Clod": "ExplosionClodMesh",
    "SM_FX_Scorch": "ExplosionScorchMesh",
}

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
lines = []


def log(s):
    lines.append(s)
    unreal.log(s)


def expr(mat, cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


def build_material():
    eal.make_directory(ROOT)
    if eal.does_asset_exist(MASTER):
        mat = eal.load_asset(MASTER)
        mel.delete_all_material_expressions(mat)
    else:
        mat = tools.create_asset("M_FX_Master", ROOT, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("opacity_mask_clip_value", 0.5)
    mat.set_editor_property("two_sided", False)

    opacity = expr(mat, unreal.MaterialExpressionScalarParameter, -900, 400)
    opacity.set_editor_property("parameter_name", "Opacity")
    opacity.set_editor_property("default_value", 1.0)
    opacity.set_editor_property("use_custom_primitive_data", True)
    opacity.set_editor_property("primitive_data_index", 0)

    emissive = expr(mat, unreal.MaterialExpressionScalarParameter, -900, 150)
    emissive.set_editor_property("parameter_name", "Emissive")
    emissive.set_editor_property("default_value", 0.0)
    emissive.set_editor_property("use_custom_primitive_data", True)
    emissive.set_editor_property("primitive_data_index", 1)

    color = expr(mat, unreal.MaterialExpressionVectorParameter, -900, -150)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.3, 0.28, 0.25, 1.0))
    color.set_editor_property("use_custom_primitive_data", True)
    color.set_editor_property("primitive_data_index", 2)

    # Facet shade for the glow: 0.45 on faces turned away from the light, 1 on top.
    normal = expr(mat, unreal.MaterialExpressionVertexNormalWS, -900, 0)
    light_dir = expr(mat, unreal.MaterialExpressionConstant3Vector, -900, 60)
    light_dir.set_editor_property("constant", unreal.LinearColor(0.33, 0.22, 0.92, 0.0))
    dot = expr(mat, unreal.MaterialExpressionDotProduct, -650, 20)
    mel.connect_material_expressions(normal, "", dot, "A")
    mel.connect_material_expressions(light_dir, "", dot, "B")
    half = expr(mat, unreal.MaterialExpressionConstantBiasScale, -480, 20)  # (x + 1) * 0.5
    half.set_editor_property("bias", 1.0)
    half.set_editor_property("scale", 0.5)
    mel.connect_material_expressions(dot, "", half, "")
    shade = expr(mat, unreal.MaterialExpressionLinearInterpolate, -300, 20)
    shade.set_editor_property("const_a", 0.45)
    shade.set_editor_property("const_b", 1.0)
    mel.connect_material_expressions(half, "", shade, "Alpha")

    glow = expr(mat, unreal.MaterialExpressionMultiply, -480, 150)
    mel.connect_material_expressions(color, "", glow, "A")
    mel.connect_material_expressions(emissive, "", glow, "B")
    glow_shaded = expr(mat, unreal.MaterialExpressionMultiply, -150, 120)
    mel.connect_material_expressions(glow, "", glow_shaded, "A")
    mel.connect_material_expressions(shade, "", glow_shaded, "B")

    # DitherTemporalAA is an engine material function in UE5, not an expression class.
    dither = expr(mat, unreal.MaterialExpressionMaterialFunctionCall, -480, 400)
    dither.set_editor_property("material_function", unreal.load_asset(
        "/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA.DitherTemporalAA"))
    pins = [str(n) for n in mel.get_material_expression_input_names(dither)]
    mel.connect_material_expressions(opacity, "", dither, pins[0] if pins else "Alpha Threshold")
    log("dither inputs: %s" % pins)

    rough = expr(mat, unreal.MaterialExpressionConstant, -300, 260)
    rough.set_editor_property("r", 1.0)
    spec = expr(mat, unreal.MaterialExpressionConstant, -300, 320)
    spec.set_editor_property("r", 0.15)

    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(glow_shaded, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(dither, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("material %s: %d expressions" % (MASTER, mel.get_num_material_expressions(mat)))
    return mat


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
    smd.set_editor_property("generate_lightmap_u_vs", False)
    smd.set_editor_property("remove_degenerates", True)
    smd.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    return ui


def run():
    mat = build_material()
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    try:
        tasks = []
        for name in MESHES:
            t = unreal.AssetImportTask()
            t.set_editor_property("filename", os.path.join(FBX_DIR, name + ".fbx"))
            t.set_editor_property("destination_path", ROOT)
            t.set_editor_property("destination_name", name)
            t.set_editor_property("automated", True)
            t.set_editor_property("replace_existing", True)
            t.set_editor_property("save", False)
            t.set_editor_property("options", import_options())
            tasks.append(t)
        tools.import_asset_tasks(tasks)
    finally:
        unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 1")

    visuals = eal.load_asset(VISUALS)
    bad = 0
    for name, field in MESHES.items():
        mesh = eal.load_asset("%s/%s" % (ROOT, name))
        if not isinstance(mesh, unreal.StaticMesh):
            log("MISSING %s" % name)
            bad += 1
            continue
        for i in range(len(mesh.get_editor_property("static_materials"))):
            mesh.set_material(i, mat)
        body = mesh.get_editor_property("body_setup")
        if body:
            body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        box = mesh.get_bounding_box()
        log("%-14s x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f" % (
            name, box.min.x, box.max.x, box.min.y, box.max.y, box.min.z, box.max.z))
        eal.save_loaded_asset(mesh)
        if visuals:
            visuals.set_editor_property(field, mesh)
    if visuals:
        visuals.set_editor_property("ExplosionMaterial", mat)
        eal.save_loaded_asset(visuals)
        log("DA_SoldierVisuals effects set")
    else:
        log("MISSING %s" % VISUALS)
        bad += 1
    log("done, %d problems" % bad)


try:
    run()
except Exception as e:  # keep the report even on failure
    import traceback
    log("FAILED: %s\n%s" % (e, traceback.format_exc()))
finally:
    with open(REPORT, "w") as f:
        f.write("\n".join(lines) + "\n")
