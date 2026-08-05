#pragma once
#include <cstdint>
#include <string>

namespace trellis {

// Cross-module runtime flags. Set from parsed params at the start of trellis_run;
// the modules that own them read them with an environment fallback so test
// binaries (which don't parse args) keep their historical TRELLIS_* behavior.
extern bool g_sparse_cast_f32;  // defined in sparse.cpp        (TRELLIS_F32)
extern bool g_c2s_diagnostics;  // defined in sparse.cpp
extern bool g_no_fa;            // defined in dit.cpp           (TRELLIS_NOFA)
extern bool g_require_gpu;      // defined in trellis_model.cpp (TRELLIS_REQUIRE_GPU)

// Every knob for one TRELLIS.2 image->3D run. Resolved as default -> environment
// (the historical TRELLIS_* / GSS / GSH names) -> CLI flag, with the CLI winning.
// trellis-cli and trellis-server share the parser: the server runs it once for its
// launch defaults, then per request to apply overrides (resolution, bg removal, ...).
struct TrellisParams {
    std::string image;                                          // input image (image->3D)
    std::string output = "model.glb";                           // output .glb
    std::string copyright;                                      // glTF asset.copyright metadata
    std::string models = "models";              // GGUF dir; override with --models DIR
    std::string host   = "127.0.0.1";                           // trellis-server only
    int      port = 8080;                                       // trellis-server only
    int      gpu  = 0;                                          // >=0 GPU index, <0 CPU
    uint32_t seed = 0;

    bool cascade    = true;     // 1024 cascade (default); --res 512 selects the light path
    int  hr_res     = 1024;     // HR cascade target resolution (1024 / 1536)
    int  max_tokens = 49152;    // HR token budget (backoff floors at 1024)

    int birefnet = -1;          // bg removal: 1 BiRefNet, 0 white-threshold, -1 auto
                                // (auto: keep a pre-matted image's alpha; else BiRefNet when
                                // birefnet.gguf is present; threshold as last resort. The
                                // threshold matte reads specular highlights [min(RGB)>=232]
                                // as background and the model then generates holes there.)
    bool texture  = true;       // texture flow + UV bake (else geometry-only)
    bool xatlas   = true;       // xatlas UV unwrap (else voxel-native box projection)
    int  band     = 0;          // narrow-band DC remesh band width (remesh_dc.h).
                                //   0 = auto: scale with resolution (res/512) so the
                                //   smoothing offset is resolution-independent — 1 @512,
                                //   2 @1024 — which suppresses the res-1024 "outer-skin"
                                //   speckle (issue #22). >0 forces that width (e.g. 1 for
                                //   the thin-wall reference look, 2 for a thicker shell).
    int  decim    = -1;         // decimation cluster grid   (-1 => per-cascade default)
    int  tex      = -1;         // UV atlas size in px        (-1 => per-cascade default)
    int  tex_res  = -1;         // texture PBR resolution: -1 => auto (drop dense res-1024 tex to
                                //   512, whose clean coarse PBR bakes onto the res-1024 mesh
                                //   without the partial-coverage "skin" speckle); else force 512/1024
    int  webp     = -1;         // GLB texture encoding: -1 auto (WebP if built with it), 1 on, 0 off (PNG)
    bool f32      = false;      // f32 sparse-conv compute
    bool no_fa    = false;      // disable FlashAttention (manual softmax)
    bool require_gpu = false;   // refuse CPU fallback if no GPU is usable
    float gss = 7.5f;           // sparse guidance; canonical field, also set by --sparse-guidance
    float gsh = 7.5f;           // shape guidance; canonical field, also set by --shape-guidance
    // Per-stage sampler controls. Defaults reproduce the original schedule.
    int   sparse_steps = 12;
    float sparse_guidance_rescale = 0.7f;
    float sparse_guidance_interval_start = 0.6f;
    float sparse_guidance_interval_end = 1.0f;
    float sparse_rescale_t = 5.0f;
    int   shape_steps = 12;
    float shape_guidance_rescale = 0.5f;
    float shape_guidance_interval_start = 0.6f;
    float shape_guidance_interval_end = 1.0f;
    float shape_rescale_t = 3.0f;
    int   texture_steps = 12;
    float texture_guidance_strength = 1.0f;
    float texture_guidance_rescale = 0.0f;
    float texture_guidance_interval_start = 0.6f;
    float texture_guidance_interval_end = 0.9f;
    float texture_rescale_t = 3.0f;
    bool c2s_diagnostics = false; // log C2S subdivision, chunking, and graph allocation
    bool voxply = false;        // dump out/myvox.ply              (debug)
    bool dump_slat = false;     // dump /tmp/hr_slat.bin           (debug)
    bool dump_bg = false;       // also write the bg-removal cutout as <out>_cutout.png
    bool bg_only = false;       // background removal only: write the cutout and skip the rest

    bool help = false;          // --help requested

    // 512 -> light single-res path; 1024/1536 -> cascade with that HR target.
    void set_res(int res) {
        if (res <= 512) { cascade = false; hr_res = 512; }
        else            { cascade = true;  hr_res = res; }
    }
};

void print_usage(const char* argv0, bool server);

// Shared strict numeric parsing for CLI flags and HTTP multipart fields.
bool parse_strict_int(const std::string& value, int& out);
bool parse_strict_float(const std::string& value, float& out);

// Sampler names may use CLI hyphens (with an optional "--") or HTTP underscores.
bool is_sampler_option(const std::string& name);
bool set_sampler_option(const std::string& name, const std::string& value,
                        TrellisParams& p, std::string& error);
bool validate_sampler_params(const TrellisParams& p, std::string& error);

// Apply environment fallbacks, then parse argv (CLI wins). The first two bare
// (non-flag) positionals fill `image` then `output`. Returns false on a parse
// error OR when --help was requested; check p.help to tell them apart.
bool parse_args(int argc, char** argv, TrellisParams& p);

}  // namespace trellis
