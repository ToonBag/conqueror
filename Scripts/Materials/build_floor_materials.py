"""
Builds the procedural low-poly floor materials in /Game/Materials/Floors (cosmetic only).

Run in the Unreal Editor:  py Scripts/Materials/build_floor_materials.py

M_Floor_Master is one opaque material whose Custom HLSL node draws, in world space (metres), one of
five surfaces picked by the "Style" scalar:
  0 grass      - flat-shaded triangle facets with large lighter / dry patches
  1 dirt       - faceted dirt with darker damp patches and small diamond pebbles
  2 planks     - 20 cm floorboards along world X, staggered ends, dark seams and grain
  3 flagstone  - irregular bevel-shaded stones with dark mortar
  4 water      - slowly drifting faceted blues with a gentle per-facet shimmer
plus the 1 m grid lines on top (GridOrigin / CellSize are filled in by AGridManager on the ground;
on the other planes the defaults line up because the grid origin is a whole number of metres).

The MI_Floor_* instances hold the colours. Everything is world-aligned, so planes of any size tile
seamlessly with no UVs or textures. Re-running is safe: the master is rebuilt in place and the
instances are updated.
"""
import unreal

DIR = "/Game/Materials/Floors"
MASTER = f"{DIR}/M_Floor_Master"
TEMPLATE = "/Game/Materials/M_GridGround"  # already set up with a Substrate "Default Lit" front material

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

HLSL = r"""
struct FloorFn
{
    float H(float2 p) { float3 q = frac(p.xyx * 0.1031); q += dot(q, q.yzx + 33.33); return frac((q.x + q.y) * q.z); }
    float N(float2 p)
    {
        float2 i = floor(p); float2 f = frac(p); float2 u = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(H(i), H(i + float2(1, 0)), u.x), lerp(H(i + float2(0, 1)), H(i + float2(1, 1)), u.x), u.y);
    }
    float F(float2 p)
    {
        float s = 0.0; float a = 0.5;
        for (int k = 0; k < 4; k++) { s += a * N(p); p = p * 2.03 + 17.13; a *= 0.5; }
        return s / 0.9375;
    }
};
FloorFn L;

float2 m = WorldPos.xy / 100.0;                 // metres
float aa = max(fwidth(m.x), fwidth(m.y));       // metres per pixel, for soft edges

// Low-poly facets: each FacetSize square is split along a random diagonal into two flat triangles.
float s = max(FacetSize, 0.05);
float2 ci = floor(m / s);
float2 cf = frac(m / s);
float flip = step(0.5, L.H(ci + 7.7));
float2 tf = float2(lerp(cf.x, 1.0 - cf.x, flip), cf.y);
float upper = step(1.0, tf.x + tf.y);
float2 fl = lerp(float2(0.33, 0.33), float2(0.67, 0.67), upper);
fl.x = lerp(fl.x, 1.0 - fl.x, flip);
float2 fc = (ci + fl) * s;                      // facet centre, metres
float fr = L.H(ci * 1.37 + upper * 19.1 + 3.3);
float fr2 = L.H(ci * 2.11 + upper * 7.9 + 61.0);
float shade = 1.0 + (fr - 0.5) * Variation;

float3 A = GroundColor.rgb;
float3 B = ColorB.rgb;
float3 C = ColorC.rgb;
float3 col = A;
int st = (int)round(Style);

if (st == 0)            // grass
{
    float big = L.F(fc * 0.09 + 3.1);
    float mid = L.F(fc * 0.37 + 11.7);
    col = lerp(A, B, smoothstep(0.35, 0.75, big * 0.7 + mid * 0.3));
    float dry = smoothstep(0.60, 0.74, L.F(fc * 0.05 + 41.0));
    col = lerp(col, C, dry * 0.75);
    col *= lerp(1.0, 0.82, step(0.93, fr2));    // the odd darker tuft facet
    col *= shade;
}
else if (st == 1)       // dirt
{
    float big = L.F(fc * 0.15 + 5.0);
    col = lerp(A, B, smoothstep(0.3, 0.8, big));
    col = lerp(col, col * 0.78, smoothstep(0.60, 0.76, L.F(m * 0.4 + 71.0)));
    col *= shade;
    float2 pc = m * 4.0; float2 pi = floor(pc);
    float2 d = frac(pc) - (0.3 + 0.4 * float2(L.H(pi + 1.3), L.H(pi + 8.9)));
    float rad = 0.07 + 0.09 * L.H(pi + 4.2);
    float dia = abs(d.x) + abs(d.y);
    float peb = step(0.6, L.H(pi + 2.6)) * (1.0 - smoothstep(rad, rad + aa * 4.0, dia));
    float3 pcol = C * (0.85 + 0.3 * L.H(pi + 6.6)) * ((d.x + d.y) < 0.0 ? 1.18 : 0.82);   // lit / shaded facet
    col = lerp(col, pcol, peb);
}
else if (st == 2)       // planks
{
    float pw = 0.2; float pl = 1.6;
    float row = floor(m.y / pw);
    float along = m.x + L.H(float2(row, 3.0)) * pl;
    float seg = floor(along / pl);
    float2 id = float2(row, seg);
    float r = L.H(id + 5.5);
    col = lerp(A, B, r);
    float grain = L.N(float2(along * 1.3, frac(m.y / pw) * 5.0) + id * 13.0);
    col *= 0.9 + 0.2 * grain;
    col *= 1.0 + (L.F(m * 0.35 + 9.0) - 0.5) * 0.25;   // wear
    float ey = min(frac(m.y / pw), 1.0 - frac(m.y / pw)) * pw;
    float ex = min(frac(along / pl), 1.0 - frac(along / pl)) * pl;
    float seam = 1.0 - smoothstep(0.005, 0.005 + aa * 1.5, min(ey, ex));
    col = lerp(col, C, seam);
}
else if (st == 3)       // flagstone
{
    float sz = 0.45;
    float2 g = m / sz; float2 gi = floor(g); float2 gf = frac(g);
    float f1 = 8.0; float f2 = 8.0; float2 best = gi; float2 bp = 0;
    for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++)
    {
        float2 o = float2(x, y);
        float2 p = o + 0.15 + 0.7 * float2(L.H(gi + o + 0.7), L.H(gi + o + 5.1));
        float dd = length(p - gf);
        if (dd < f1) { f2 = f1; f1 = dd; best = gi + o; bp = p; }
        else if (dd < f2) { f2 = dd; }
    }
    float edge = (f2 - f1) * 0.5 * sz;          // ~ metres to the stone's border
    col = lerp(A, B, L.H(best + 2.2)) * (0.92 + 0.16 * L.H(best + 9.1));
    col *= dot(normalize(gf - bp + 0.0001), float2(-0.707, 0.707)) > 0.0 ? 1.07 : 0.94;   // bevel facets
    float mortar = 1.0 - smoothstep(0.018, 0.018 + aa * 1.5, edge);
    col = lerp(col, C, mortar);
}
else                    // water
{
    float w1 = L.F(fc * 0.3 + float2(Time * 0.05, Time * 0.03));
    col = lerp(A, B, smoothstep(0.3, 0.8, w1));
    float tw = sin(Time * 1.3 + fr * 6.283) * 0.5 + 0.5;
    col *= 1.0 + (tw - 0.5) * Variation * 1.5;
    col = lerp(col, C, step(0.9, fr2) * smoothstep(0.7, 1.0, tw) * 0.12);   // faint sky glint on a few facets
}

// 1 m grid lines
float2 gp = (WorldPos.xy - GridOrigin.xy) / CellSize;
float2 dl = abs(frac(gp + 0.5) - 0.5) * CellSize;
float2 fw = max(fwidth(WorldPos.xy), 0.001);
float lm = 1.0 - saturate((min(dl.x, dl.y) - LineWidth * 0.5) / max(fw.x, fw.y));
return lerp(col, LineColor.rgb, lm * LineOpacity);
"""

# name -> (default, kind)   kind: "s" scalar, "v" vector
PARAMS = {
    "GridOrigin": ((0.0, 0.0, 0.0), "v"),
    "CellSize": (100.0, "s"),
    "LineWidth": (3.0, "s"),
    "LineOpacity": (0.5, "s"),
    "LineColor": ((0.10, 0.14, 0.06), "v"),
    "GroundColor": ((0.13, 0.20, 0.07), "v"),
    "ColorB": ((0.22, 0.28, 0.10), "v"),
    "ColorC": ((0.35, 0.30, 0.14), "v"),
    "Style": (0.0, "s"),
    "FacetSize": (0.5, "s"),
    "Variation": (0.12, "s"),
}

# Linear colours. Style, facet size and per-facet variation per surface.
INSTANCES = {
    "MI_Floor_Grass": dict(Style=0, FacetSize=0.5, Variation=0.12, Roughness=0.9, LineOpacity=0.55,
                           GroundColor=(0.072, 0.092, 0.042), ColorB=(0.115, 0.128, 0.058),
                           ColorC=(0.170, 0.148, 0.080), LineColor=(0.045, 0.056, 0.028)),
    "MI_Floor_Dirt": dict(Style=1, FacetSize=0.35, Variation=0.14, Roughness=0.95, LineOpacity=0.35,
                          GroundColor=(0.150, 0.095, 0.050), ColorB=(0.210, 0.145, 0.080),
                          ColorC=(0.150, 0.130, 0.105), LineColor=(0.090, 0.058, 0.030)),
    "MI_Floor_Planks": dict(Style=2, FacetSize=0.5, Variation=0.0, Roughness=0.7, LineOpacity=0.25,
                            GroundColor=(0.170, 0.095, 0.045), ColorB=(0.270, 0.165, 0.080),
                            ColorC=(0.035, 0.022, 0.012), LineColor=(0.080, 0.050, 0.025)),
    "MI_Floor_Flagstone": dict(Style=3, FacetSize=0.5, Variation=0.0, Roughness=0.85, LineOpacity=0.25,
                               GroundColor=(0.150, 0.140, 0.120), ColorB=(0.220, 0.205, 0.180),
                               ColorC=(0.060, 0.052, 0.040), LineColor=(0.070, 0.065, 0.055)),
    "MI_Floor_Water": dict(Style=4, FacetSize=0.6, Variation=0.15, Roughness=0.15, LineOpacity=0.15,
                           GroundColor=(0.020, 0.075, 0.100), ColorB=(0.060, 0.170, 0.200),
                           ColorC=(0.550, 0.650, 0.650), LineColor=(0.020, 0.060, 0.080)),
}


def lc(rgb):
    return unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0)


def build_master():
    if not EAL.does_asset_exist(MASTER):
        if not EAL.duplicate_asset(TEMPLATE, MASTER):
            raise RuntimeError(f"Could not duplicate {TEMPLATE} to {MASTER}")
    mat = unreal.load_asset(MASTER)

    # Keep only the Substrate front material; rebuild the rest.
    substrate = None
    for e in list(MEL.get_material_expressions(mat)):
        if isinstance(e, unreal.MaterialExpressionSubstrateShadingModels) and substrate is None:
            substrate = e
        else:
            MEL.delete_material_expression(mat, e)
    if substrate is None:
        raise RuntimeError("M_Floor_Master has no Substrate Shading Models node")

    custom = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -700, 0)
    custom.set_editor_property("description", "Low-poly floor + 1m grid")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("code", HLSL)
    names = ["WorldPos", "Time"] + list(PARAMS.keys())
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)

    wp = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1100, -300)
    MEL.connect_material_expressions(wp, "", custom, "WorldPos")
    tm = MEL.create_material_expression(mat, unreal.MaterialExpressionTime, -1100, -200)
    MEL.connect_material_expressions(tm, "", custom, "Time")

    y = -100
    for name, (default, kind) in PARAMS.items():
        if kind == "s":
            p = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1100, y)
            p.set_editor_property("default_value", default)
            out = ""
        else:
            p = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -1100, y)
            p.set_editor_property("default_value", lc(default))
            out = "RGB"
        p.set_editor_property("parameter_name", name)
        MEL.connect_material_expressions(p, out, custom, name)
        y += 110

    rough = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 400)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.9)

    if not MEL.connect_material_expressions(custom, "", substrate, "BaseColor"):
        raise RuntimeError("Could not connect the Custom node to BaseColor")
    if not MEL.connect_material_expressions(rough, "", substrate, "Roughness"):
        unreal.log_warning("Floors: could not connect Roughness")

    MEL.recompile_material(mat)
    EAL.save_asset(MASTER, only_if_is_dirty=False)
    return mat


def build_instances(master):
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    for name, values in INSTANCES.items():
        path = f"{DIR}/{name}"
        mi = unreal.load_asset(path) if EAL.does_asset_exist(path) else tools.create_asset(
            name, DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, master)
        for k, v in values.items():
            if isinstance(v, tuple):
                MEL.set_material_instance_vector_parameter_value(mi, k, lc(v))
            else:
                MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
        MEL.update_material_instance(mi)
        EAL.save_asset(path, only_if_is_dirty=False)


def build():
    master = build_master()
    build_instances(master)
    print("Floor materials built:", ", ".join(INSTANCES))


build()
