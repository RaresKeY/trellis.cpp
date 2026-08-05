// post-replay — re-run the post-neural stages (weld, hole fill, decimation, UV
// bake, GLB write) from a TRELLIS_DUMP_POST dump, skipping the ~10-minute
// neural pipeline. Development harness for iterating on mesh/texture
// post-processing.
//
//   post-replay <dump.bin> <out.glb> [--box-uv] [--faces N] [--atlas T]
//               [--also-atlas T] [--webp on|off] [--meshopt] [--decim GRID]
//               [--no-weld] [--no-fill]
#include "uv_bake.h"
#include "tri_bvh.h"
#include "remesh_dc.h"
#include "mesh_glb.h"
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using trellis::VoxelPbr;

static double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void print_usage(FILE* stream, const char* argv0) {
    fprintf(stream,
        "usage: %s <dump.bin> <out.glb> [options]\n"
        "\n"
        "options:\n"
        "  --box-uv          use box-projected UVs\n"
        "  --faces N         target face count (default 300000)\n"
        "  --meshopt          use guarded meshoptimizer simplification; QEM is the default\n"
        "  --atlas T         primary texture atlas size (default 2048)\n"
        "  --also-atlas T    write a second GLB from the same mesh at atlas size T\n"
        "  --webp on|off     request WebP textures or force PNG (default on; PNG fallback)\n"
        "  --decim GRID      cluster decimation grid; 0 copies the remeshed mesh\n"
        "  --band N          narrow-band remesh width (default 1)\n"
        "  --no-weld         skip initial vertex welding\n"
        "  --no-fill         skip initial small-hole filling\n"
        "  --no-bake         stop before UV baking and GLB writing\n"
        "  --no-remesh       skip narrow-band remeshing\n"
        "  --no-snap         disable texture lookup snapping\n"
        "  -h, --help        show this help\n",
        argv0);
}

static bool parse_int_option(const char* option, const char* value,
                             int min_value, int max_value, int& result) {
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0' ||
        parsed < min_value || parsed > max_value) {
        fprintf(stderr, "%s expects an integer in [%d, %d], got '%s'\n",
                option, min_value, max_value, value);
        return false;
    }
    result = static_cast<int>(parsed);
    return true;
}

// boundary/non-manifold audit over the welded index space (positions assumed welded)
#include <unordered_map>
static void audit(const char* tag, const std::vector<int32_t>& faces) {
    std::unordered_map<uint64_t,int> e;
    e.reserve(faces.size() * 2);
    const size_t F = faces.size() / 3;
    auto k = [](int a, int b){ if (a>b){int t=a;a=b;b=t;} return ((uint64_t)(uint32_t)a<<32)|(uint32_t)b; };
    for (size_t f = 0; f < F; ++f)
        for (int j = 0; j < 3; ++j) e[k(faces[3*f+j], faces[3*f+(j+1)%3])]++;
    size_t nb = 0, nm = 0;
    for (auto& kv : e) { if (kv.second == 1) ++nb; else if (kv.second > 2) ++nm; }
    printf("  [audit] %-22s F=%-9zu boundary_edges=%-7zu nonmanifold=%zu\n", tag, F, nb, nm);
    fflush(stdout);
}

int main(int argc, char** argv) {
    if (argc == 2 && (!std::strcmp(argv[1], "-h") || !std::strcmp(argv[1], "--help"))) {
        print_usage(stdout, argv[0]);
        return 0;
    }
    if (argc < 3) {
        print_usage(stderr, argv[0]);
        return 1;
    }

    const char* dump = argv[1];
    const char* out = argv[2];
    bool boxuv = false, do_weld = true, do_fill = true, do_bake = true, do_remesh = true, do_snap = true;
    bool use_webp = true, use_meshopt = false, decim_set = false;
    int band = 1;
    int faces_target = 300000, atlas = 2048, also_atlas = 0, decim = -1;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        auto parse_next_int = [&](int min_value, int max_value, int& result) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s requires a value\n", a.c_str());
                return false;
            }
            return parse_int_option(a.c_str(), argv[++i], min_value, max_value, result);
        };

        if (a == "--box-uv") boxuv = true;
        else if (a == "--faces") {
            if (!parse_next_int(1, INT_MAX, faces_target)) return 2;
        } else if (a == "--atlas") {
            if (!parse_next_int(1, INT_MAX, atlas)) return 2;
        } else if (a == "--also-atlas") {
            if (!parse_next_int(1, INT_MAX, also_atlas)) return 2;
        } else if (a == "--webp") {
            if (i + 1 >= argc) {
                fprintf(stderr, "--webp requires on or off\n");
                return 2;
            }
            const std::string value = argv[++i];
            if (value == "on") use_webp = true;
            else if (value == "off") use_webp = false;
            else {
                fprintf(stderr, "--webp must be on or off, got '%s'\n", value.c_str());
                return 2;
            }
        } else if (a == "--meshopt") {
            use_meshopt = true;
        } else if (a == "--decim") {
            if (!parse_next_int(INT_MIN, INT_MAX, decim)) return 2;
            decim_set = true;
        } else if (a == "--no-weld") do_weld = false;
        else if (a == "--no-fill") do_fill = false;
        else if (a == "--no-bake") do_bake = false;
        else if (a == "--no-remesh") do_remesh = false;
        else if (a == "--band") {
            if (!parse_next_int(1, INT_MAX, band)) return 2;
        } else if (a == "--no-snap") do_snap = false;
        else if (a == "-h" || a == "--help") {
            print_usage(stdout, argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", a.c_str());
            return 2;
        }
    }
    if (use_meshopt && decim_set) {
        fprintf(stderr, "--meshopt cannot be combined with --decim\n");
        return 2;
    }

    FILE* f = fopen(dump, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", dump); return 1; }
    int V, F, Mv, res;
    if (fread(&V,4,1,f)+fread(&F,4,1,f)+fread(&Mv,4,1,f)+fread(&res,4,1,f) != 4) return 1;
    std::vector<float> verts((size_t)V*3);
    std::vector<int32_t> faces((size_t)F*3);
    std::vector<std::array<int,3>> coords((size_t)Mv);
    std::vector<float> pbr6((size_t)Mv*6);
    if (fread(verts.data(),4,verts.size(),f) != verts.size()) return 1;
    if (fread(faces.data(),4,faces.size(),f) != faces.size()) return 1;
    for (auto& c : coords) if (fread(c.data(),4,3,f) != 3) return 1;
    if (fread(pbr6.data(),4,pbr6.size(),f) != pbr6.size()) return 1;
    fclose(f);
    printf("loaded: V=%d F=%d voxels=%d res=%d\n", V, F, Mv, res);

    double t = now();
    if (do_weld) trellis::weld_vertices(verts, faces, nullptr, 1.0f / ((float)res * 8.0f));
    printf("  [weld %.1fs]\n", now()-t); t = now();
    audit("weld", faces);
    if (do_fill) trellis::fill_small_holes(faces);
    printf("  [fill %.1fs]\n", now()-t); t = now();
    audit("fill_small_holes", faces);

    trellis::TriBvh bvh = trellis::TriBvh::build(verts.data(), (int64_t)verts.size()/3,
                                                 faces.data(), (int64_t)faces.size()/3);
    printf("  [bvh %.1fs]\n", now()-t); t = now();
    trellis::Mesh rm;
    if (do_remesh) {
        rm = trellis::remesh_narrow_band_dc(verts.data(), (int64_t)verts.size()/3,
                                            faces.data(), (int64_t)faces.size()/3, bvh, res, band);
        printf("  [remesh %.1fs]\n", now()-t); t = now();
        audit("remesh", rm.faces);
        // match the CLI: clean degenerates/unify winding, drop floater components
        if (rm.F() > 0) {
            trellis::clean_mesh(rm.V(), rm.faces);
            audit("clean_mesh", rm.faces);
            int ndrop = trellis::drop_small_components(rm.verts, rm.faces, 0.02f);
            printf("  [clean+drop %.1fs] dropped=%d\n", now()-t, ndrop); t = now();
            audit("drop_components", rm.faces);
        }
    }
    const std::vector<float>& sverts = rm.F() > 0 ? rm.verts : verts;
    const std::vector<int32_t>& sfaces = rm.F() > 0 ? rm.faces : faces;

    std::vector<float> dv, dp; std::vector<int32_t> df;
    if (decim > 0) trellis::decimate_cluster(sverts, (int)sverts.size()/3, sfaces, (int)sfaces.size()/3, {}, decim, dv, df, dp);
    else if (decim == 0) { dv = sverts; df = sfaces; }
    else {
        if (use_meshopt) {
            trellis::decimate_simplify(
                sverts, (int)sverts.size()/3, sfaces, (int)sfaces.size()/3,
                faces_target, dv, df, trellis::SimplifyFallback::None);
            audit("decimate_meshopt", df);
            const int actual_faces = (int)df.size() / 3;
            if (actual_faces > faces_target) {
                fprintf(stderr,
                        "warning: guarded meshoptimizer stopped at %d faces "
                        "(target %d); no FQMS fallback was used\n",
                        actual_faces, faces_target);
            }
        } else {
            // Match the CLI's faithful CuMesh QEM port.
            trellis::decimate_qem(
                sverts, (int)sverts.size()/3, sfaces, (int)sfaces.size()/3,
                faces_target, dv, df);
            audit("decimate_qem", df);
        }
        trellis::weld_vertices(dv, df, nullptr, 1.0f / ((float)res * 8.0f));
        audit("weld2", df);
        trellis::fill_small_holes(df);
        audit("fill2", df);
        int ndrop2 = trellis::drop_small_components(dv, df, 0.03f);
        if (ndrop2) printf("  dropped %d more comps\n", ndrop2);
        audit("drop2", df);
    }
    printf("  [decimate %.1fs]\n", now()-t); t = now();
    if (!do_bake) { printf("(--no-bake) done\n"); return 0; }

    VoxelPbr vox{&coords, &pbr6, res, do_snap ? &bvh : nullptr};
    const std::vector<float> no_vp;
    auto bake_and_write = [&](int atlas_size, const char* output) -> bool {
        const double bake_start = now();
        trellis::BakedMesh bm = boxuv
            ? trellis::uv_box_project(dv, (int)dv.size()/3, df, (int)df.size()/3,
                                      no_vp, atlas_size, &vox)
            : trellis::uv_bake(dv, (int)dv.size()/3, df, (int)df.size()/3,
                               no_vp, atlas_size, &vox);
        if (!boxuv && !bm.ok())
            bm = trellis::uv_chart_project(dv, (int)dv.size()/3, df, (int)df.size()/3,
                                           no_vp, atlas_size, &vox);
        printf("  [bake atlas=%d %.1fs]\n", atlas_size, now() - bake_start);
        if (!bm.ok()) {
            fprintf(stderr, "bake failed for atlas %d\n", atlas_size);
            return false;
        }
        printf("  [audit] bake atlas=%d: faces in=%zu out=%zu (dropped %lld)\n",
               atlas_size, df.size()/3, bm.faces.size()/3,
               (long long)(df.size()/3) - (long long)(bm.faces.size()/3));
        if (!trellis::write_glb_textured(
                output, bm.verts.data(), (int64_t)bm.verts.size()/3, bm.uv.data(),
                bm.faces.data(), (int64_t)bm.faces.size()/3,
                bm.base.data(), bm.mr.data(), bm.T,
                /*double_sided=*/rm.F() == 0, /*seed=*/-1,
                /*copyright=*/nullptr, use_webp)) {
            fprintf(stderr, "cannot write %s\n", output);
            return false;
        }
        printf("wrote %s (atlas %d)\n", output, bm.T);
        return true;
    };
    if (!bake_and_write(atlas, out)) return 1;
    if (also_atlas > 0) {
        std::string also_out(out);
        const std::string suffix = "-atlas" + std::to_string(also_atlas);
        if (also_out.size() >= 4 &&
            also_out.compare(also_out.size() - 4, 4, ".glb") == 0)
            also_out.insert(also_out.size() - 4, suffix);
        else
            also_out += suffix + ".glb";
        if (!bake_and_write(also_atlas, also_out.c_str())) return 1;
    }
    return 0;
}
