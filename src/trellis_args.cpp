#include "trellis_args.h"

#include <cerrno>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace trellis {
namespace {

enum class SamplerField {
    none,
    sparse_steps, sparse_guidance, sparse_rescale, sparse_start, sparse_end, sparse_rescale_t,
    shape_steps, shape_guidance, shape_rescale, shape_start, shape_end, shape_rescale_t,
    texture_steps, texture_guidance, texture_rescale, texture_start, texture_end, texture_rescale_t,
};

std::string sampler_key(std::string name) {
    if (name.compare(0, 2, "--") == 0) name.erase(0, 2);
    for (char& c : name) if (c == '-') c = '_';
    return name;
}

SamplerField sampler_field(const std::string& name) {
    const std::string key = sampler_key(name);
    if (key == "sparse_steps") return SamplerField::sparse_steps;
    if (key == "sparse_guidance" || key == "sparse_guidance_strength" || key == "gss")
        return SamplerField::sparse_guidance;
    if (key == "sparse_guidance_rescale") return SamplerField::sparse_rescale;
    if (key == "sparse_guidance_start" || key == "sparse_guidance_interval_start")
        return SamplerField::sparse_start;
    if (key == "sparse_guidance_end" || key == "sparse_guidance_interval_end")
        return SamplerField::sparse_end;
    if (key == "sparse_rescale_t") return SamplerField::sparse_rescale_t;

    if (key == "shape_steps") return SamplerField::shape_steps;
    if (key == "shape_guidance" || key == "shape_guidance_strength" || key == "gsh")
        return SamplerField::shape_guidance;
    if (key == "shape_guidance_rescale") return SamplerField::shape_rescale;
    if (key == "shape_guidance_start" || key == "shape_guidance_interval_start")
        return SamplerField::shape_start;
    if (key == "shape_guidance_end" || key == "shape_guidance_interval_end")
        return SamplerField::shape_end;
    if (key == "shape_rescale_t") return SamplerField::shape_rescale_t;

    if (key == "texture_steps") return SamplerField::texture_steps;
    if (key == "texture_guidance" || key == "texture_guidance_strength")
        return SamplerField::texture_guidance;
    if (key == "texture_guidance_rescale") return SamplerField::texture_rescale;
    if (key == "texture_guidance_start" || key == "texture_guidance_interval_start")
        return SamplerField::texture_start;
    if (key == "texture_guidance_end" || key == "texture_guidance_interval_end")
        return SamplerField::texture_end;
    if (key == "texture_rescale_t") return SamplerField::texture_rescale_t;
    return SamplerField::none;
}

bool edge_space(const std::string& text) {
    return !text.empty() &&
           (std::isspace(static_cast<unsigned char>(text.front())) ||
            std::isspace(static_cast<unsigned char>(text.back())));
}

bool strict_int(const std::string& text, int& result) {
    if (text.empty() || edge_space(text)) return false;
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno == ERANGE || end != text.c_str() + text.size() ||
        parsed < INT_MIN || parsed > INT_MAX)
        return false;
    result = static_cast<int>(parsed);
    return true;
}

bool strict_float(const std::string& text, float& result) {
    if (text.empty() || edge_space(text)) return false;
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (errno == ERANGE || end != text.c_str() + text.size() || !std::isfinite(parsed))
        return false;
    result = parsed;
    return true;
}

bool validate_stage(const char* stage, int steps, float guidance, float guidance_rescale,
                    float interval_start, float interval_end, float rescale_t,
                    std::string& error) {
    if (steps < 1 || steps > 1000) {
        error = std::string(stage) + "_steps must be between 1 and 1000";
        return false;
    }
    if (!std::isfinite(guidance) || guidance < 0.0f) {
        error = std::string(stage) + "_guidance must be finite and non-negative";
        return false;
    }
    if (!std::isfinite(guidance_rescale) ||
        guidance_rescale < 0.0f || guidance_rescale > 1.0f) {
        error = std::string(stage) + "_guidance_rescale must be between 0 and 1";
        return false;
    }
    if (!std::isfinite(interval_start) || !std::isfinite(interval_end) ||
        interval_start < 0.0f || interval_start > interval_end || interval_end > 1.0f) {
        error = std::string(stage) +
                "_guidance interval must satisfy 0 <= start <= end <= 1";
        return false;
    }
    if (!std::isfinite(rescale_t) || rescale_t <= 0.0f) {
        error = std::string(stage) + "_rescale_t must be finite and positive";
        return false;
    }
    return true;
}

}  // namespace

bool parse_strict_int(const std::string& value, int& out) {
    return strict_int(value, out);
}

bool parse_strict_float(const std::string& value, float& out) {
    return strict_float(value, out);
}

bool is_sampler_option(const std::string& name) {
    return sampler_field(name) != SamplerField::none;
}

bool set_sampler_option(const std::string& name, const std::string& value,
                        TrellisParams& p, std::string& error) {
    const SamplerField field = sampler_field(name);
    if (field == SamplerField::none) {
        error = "unknown sampler option: " + name;
        return false;
    }

    int* integer = nullptr;
    float* scalar = nullptr;
    switch (field) {
        case SamplerField::sparse_steps:       integer = &p.sparse_steps; break;
        case SamplerField::shape_steps:        integer = &p.shape_steps; break;
        case SamplerField::texture_steps:      integer = &p.texture_steps; break;
        case SamplerField::sparse_guidance:    scalar = &p.gss; break;
        case SamplerField::sparse_rescale:     scalar = &p.sparse_guidance_rescale; break;
        case SamplerField::sparse_start:       scalar = &p.sparse_guidance_interval_start; break;
        case SamplerField::sparse_end:         scalar = &p.sparse_guidance_interval_end; break;
        case SamplerField::sparse_rescale_t:   scalar = &p.sparse_rescale_t; break;
        case SamplerField::shape_guidance:     scalar = &p.gsh; break;
        case SamplerField::shape_rescale:      scalar = &p.shape_guidance_rescale; break;
        case SamplerField::shape_start:        scalar = &p.shape_guidance_interval_start; break;
        case SamplerField::shape_end:          scalar = &p.shape_guidance_interval_end; break;
        case SamplerField::shape_rescale_t:    scalar = &p.shape_rescale_t; break;
        case SamplerField::texture_guidance:   scalar = &p.texture_guidance_strength; break;
        case SamplerField::texture_rescale:    scalar = &p.texture_guidance_rescale; break;
        case SamplerField::texture_start:      scalar = &p.texture_guidance_interval_start; break;
        case SamplerField::texture_end:        scalar = &p.texture_guidance_interval_end; break;
        case SamplerField::texture_rescale_t:  scalar = &p.texture_rescale_t; break;
        case SamplerField::none: break;
    }

    if (integer) {
        int parsed = 0;
        if (!parse_strict_int(value, parsed)) {
            error = sampler_key(name) + " must be a whole integer";
            return false;
        }
        *integer = parsed;
    } else {
        float parsed = 0.0f;
        if (!parse_strict_float(value, parsed)) {
            error = sampler_key(name) + " must be a finite number";
            return false;
        }
        *scalar = parsed;
    }
    error.clear();
    return true;
}

bool validate_sampler_params(const TrellisParams& p, std::string& error) {
    if (!validate_stage("sparse", p.sparse_steps, p.gss, p.sparse_guidance_rescale,
                        p.sparse_guidance_interval_start, p.sparse_guidance_interval_end,
                        p.sparse_rescale_t, error))
        return false;
    if (!validate_stage("shape", p.shape_steps, p.gsh, p.shape_guidance_rescale,
                        p.shape_guidance_interval_start, p.shape_guidance_interval_end,
                        p.shape_rescale_t, error))
        return false;
    if (!validate_stage("texture", p.texture_steps, p.texture_guidance_strength,
                        p.texture_guidance_rescale, p.texture_guidance_interval_start,
                        p.texture_guidance_interval_end, p.texture_rescale_t, error))
        return false;
    error.clear();
    return true;
}

const char* dense_policy_name(DensePolicy policy) noexcept {
    switch (policy) {
        case DensePolicy::Fail:        return "fail";
        case DensePolicy::Fallback512: return "fallback-512";
        case DensePolicy::Allow:       return "allow";
    }
    return "invalid";
}

const char* dense_guard_action_name(DenseGuardAction action) noexcept {
    switch (action) {
        case DenseGuardAction::NotApplicable: return "not-applicable";
        case DenseGuardAction::Disabled:      return "disabled";
        case DenseGuardAction::Pass:          return "pass";
        case DenseGuardAction::Fail:          return "fail";
        case DenseGuardAction::Fallback512:   return "fallback-512";
        case DenseGuardAction::Allow:         return "allow";
    }
    return "invalid";
}

bool set_density_option(const std::string& name, const std::string& value,
                        TrellisParams& p, std::string& error) {
    const std::string key = sampler_key(name);
    if (key == "max_cascade_tokens") {
        int parsed = 0;
        if (!parse_strict_int(value, parsed)) {
            error = "max_cascade_tokens must be a base-10 integer";
            return false;
        }
        p.max_cascade_tokens = parsed;
        error.clear();
        return true;
    }
    if (key == "dense_policy") {
        if (value == "fail") p.dense_policy = DensePolicy::Fail;
        else if (value == "fallback-512") p.dense_policy = DensePolicy::Fallback512;
        else if (value == "allow") p.dense_policy = DensePolicy::Allow;
        else {
            error = "dense_policy must be fail, fallback-512, or allow";
            return false;
        }
        error.clear();
        return true;
    }
    error = "unknown density option: " + name;
    return false;
}

bool validate_density_params(const TrellisParams& p, std::string& error) {
    if (p.max_cascade_tokens < 0) {
        error = "max_cascade_tokens must be non-negative";
        return false;
    }
    switch (p.dense_policy) {
        case DensePolicy::Fail:
        case DensePolicy::Fallback512:
        case DensePolicy::Allow:
            error.clear();
            return true;
    }
    error = "dense_policy is invalid";
    return false;
}

DenseGuardDecision resolve_dense_guard(bool requested_cascade,
                                       int selected_resolution,
                                       int observed_tokens,
                                       int max_cascade_tokens,
                                       DensePolicy policy) noexcept {
    DenseGuardDecision decision;
    decision.resolved_cascade = requested_cascade;
    decision.resolved_resolution = requested_cascade ? selected_resolution : 512;

    if (!requested_cascade) {
        decision.action = DenseGuardAction::NotApplicable;
        return decision;
    }
    if (max_cascade_tokens == 0) {
        decision.action = DenseGuardAction::Disabled;
        return decision;
    }
    if (observed_tokens <= max_cascade_tokens) {
        decision.action = DenseGuardAction::Pass;
        return decision;
    }
    switch (policy) {
        case DensePolicy::Fail:
            decision.action = DenseGuardAction::Fail;
            decision.proceed = false;
            return decision;
        case DensePolicy::Fallback512:
            decision.action = DenseGuardAction::Fallback512;
            decision.resolved_cascade = false;
            decision.resolved_resolution = 512;
            return decision;
        case DensePolicy::Allow:
            decision.action = DenseGuardAction::Allow;
            return decision;
    }
    decision.action = DenseGuardAction::Fail;
    decision.proceed = false;
    return decision;
}

void print_usage(const char* argv0, bool server) {
    if (server) {
        fprintf(stderr,
            "usage: %s [--host H] [--port P] [--models DIR] [--gpu N] [generation defaults...]\n",
            argv0);
    } else {
        fprintf(stderr,
            "usage: %s <image.png> <out.glb> [options]\n"
            "   or: %s --image <image.png> --output <out.glb> [options]\n",
            argv0, argv0);
    }
    fprintf(stderr,
        "\n"
        "  -i, --image PATH        input image                  (image->3D)\n"
        "  -o, --output PATH       output .glb                  (default model.glb)\n"
        "      --copyright TEXT    glTF asset.copyright metadata\n"
        "  -m, --models DIR        GGUF model directory\n"
        "      --gpu N             GPU index, <0 = CPU          (default 0)\n"
        "  -s, --seed N            RNG seed                     (default 42)\n"
        "      --res 512|1024|1536 geometry resolution\n"
        "      --max-tokens N      HR token budget              (default 49152)\n"
        "      --max-cascade-tokens N  hard post-backoff guard; 0 disables (default 0)\n"
        "      --dense-policy MODE  fail | fallback-512 | allow (default fallback-512)\n"
        "      --bg-removal MODE   threshold | birefnet   (default: auto -- a pre-matted\n"
        "                          image keeps its alpha; otherwise BiRefNet when its model\n"
        "                          is present. The plain threshold matte cuts out specular\n"
        "                          highlights, which the flow then turns into holes.)\n"
        "      --birefnet          alias for --bg-removal birefnet\n"
        "      --no-texture        geometry only\n"
        "      --xatlas            xatlas UV unwrap (default)\n"
        "      --box-uv            voxel-native box projection (faster)\n"
        "      --band N            narrow-band DC remesh band width (default: auto —\n"
        "                          res/512, i.e. 1 @512 / 2 @1024, which suppresses the\n"
        "                          res-1024 outer-skin speckle; N forces that width)\n"
        "      --decim GRID        legacy cluster-grid decimation (default: quadric\n"
        "                          simplify to 300K faces @1024 / 150K @512; 0 = none)\n"
        "      --atlas PX          UV atlas size (default 2048 @1024 / 1024 @512)\n"
        "      --tex-res N         texture PBR resolution 512/1024 (default: auto — drops\n"
        "                          a dense res-1024 decode to a clean res-512 PBR volume)\n"
        "      --webp on|off       encode GLB textures as WebP (default: on when built with\n"
        "                          WebP support; off = PNG)\n"
        "      --dump-bg           also write the background-removal cutout as <out>_cutout.png\n"
        "      --bg-only           background removal only: write the cutout and skip the rest\n"
        "      --f32               f32 sparse-conv compute\n"
        "      --no-fa             disable FlashAttention\n"
        "      --require-gpu       refuse CPU fallback\n"
        "      --<stage>-steps N  sampler steps for sparse/shape/texture\n"
        "      --<stage>-guidance F  classifier-free guidance strength\n"
        "      --<stage>-guidance-rescale F\n"
        "      --<stage>-guidance-start F  --<stage>-guidance-end F\n"
        "      --<stage>-rescale-t F\n"
        "      --gss F  --gsh F    legacy sparse/shape guidance aliases\n"
        "      --host H  --port P  trellis-server bind address\n"
        "      --c2s-diagnostics   log C2S subdivision, chunking, and graph allocation\n"
        "      --voxply            also dump the voxel point cloud as .ply\n"
        "      --dump-slat         dump the structured latent to disk\n"
        "  -h, --help              show this help\n");
}

bool parse_args(int argc, char** argv, TrellisParams& p) {
    int positional = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { fprintf(stderr, "[trellis] %s needs a value\n", name); return nullptr; }
            return argv[++i];
        };
        auto need = [&](const char* name) -> const char* {
            const char* v = next(name);
            return v;
        };

        if      (a == "-h" || a == "--help")    { p.help = true; return false; }
        else if (a == "-i" || a == "--image")   { const char* v = need(a.c_str()); if (!v) return false; p.image = v; }
        else if (a == "-o" || a == "--output")  { const char* v = need(a.c_str()); if (!v) return false; p.output = v; }
        else if (a == "--copyright")            { const char* v = need(a.c_str()); if (!v) return false; p.copyright = v; }
        else if (a == "-m" || a == "--models")  { const char* v = need(a.c_str()); if (!v) return false; p.models = v; }
        else if (a == "--gpu")                  { const char* v = need(a.c_str()); if (!v) return false; p.gpu = atoi(v); }
        else if (a == "-s" || a == "--seed")    { const char* v = need(a.c_str()); if (!v) return false; p.seed = (uint32_t)atoi(v); }
        else if (a == "--res")                  { const char* v = need(a.c_str()); if (!v) return false; p.set_res(atoi(v)); }
        else if (a == "--max-tokens")           { const char* v = need(a.c_str()); if (!v) return false; p.max_tokens = atoi(v); }
        else if (a == "--max-cascade-tokens")   { const char* v = need(a.c_str()); if (!v) return false;
                                                  std::string error;
                                                  if (!set_density_option(a, v, p, error)) {
                                                      fprintf(stderr, "[trellis] %s\n", error.c_str());
                                                      return false;
                                                  } }
        else if (a == "--dense-policy")         { const char* v = need(a.c_str()); if (!v) return false;
                                                  std::string error;
                                                  if (!set_density_option(a, v, p, error)) {
                                                      fprintf(stderr, "[trellis] %s\n", error.c_str());
                                                      return false;
                                                  } }
        else if (a == "--bg-removal")           { const char* v = need(a.c_str()); if (!v) return false; p.birefnet = (std::strcmp(v, "birefnet") == 0) ? 1 : 0; }
        else if (a == "--birefnet")             { p.birefnet = 1; }
        else if (a == "--no-texture")           { p.texture = false; }
        else if (a == "--xatlas")               { p.xatlas = true; }
        else if (a == "--box-uv")               { p.xatlas = false; }
        else if (a == "--band")                 { const char* v = need(a.c_str()); if (!v) return false; p.band = atoi(v); }
        else if (a == "--decim")                { const char* v = need(a.c_str()); if (!v) return false; p.decim = atoi(v); }
        else if (a == "--atlas" || a == "--tex"){ const char* v = need(a.c_str()); if (!v) return false; p.tex = atoi(v); }
        else if (a == "--tex-res")              { const char* v = need(a.c_str()); if (!v) return false; p.tex_res = atoi(v); }
        else if (a == "--webp")                 { const char* v = need(a.c_str()); if (!v) return false;
                                                  p.webp = (std::strcmp(v,"off")==0 || std::strcmp(v,"0")==0 || std::strcmp(v,"false")==0) ? 0
                                                         : (std::strcmp(v,"on")==0 || std::strcmp(v,"1")==0 || std::strcmp(v,"true")==0) ? 1 : -1; }
        else if (a == "--dump-bg")              { p.dump_bg = true; }
        else if (a == "--bg-only")              { p.bg_only = true; p.dump_bg = true; }
        else if (a == "--f32")                  { p.f32 = true; }
        else if (a == "--no-fa")                { p.no_fa = true; }
        else if (a == "--require-gpu")          { p.require_gpu = true; }
        else if (is_sampler_option(a))          { const char* v = need(a.c_str()); if (!v) return false;
                                                  std::string error;
                                                  if (!set_sampler_option(a, v, p, error)) {
                                                      fprintf(stderr, "[trellis] %s\n", error.c_str());
                                                      return false;
                                                  } }
        else if (a == "--host")                 { const char* v = need(a.c_str()); if (!v) return false; p.host = v; }
        else if (a == "--port")                 { const char* v = need(a.c_str()); if (!v) return false; p.port = atoi(v); }
        else if (a == "--c2s-diagnostics")      { p.c2s_diagnostics = true; }
        else if (a == "--voxply")               { p.voxply = true; }
        else if (a == "--dump-slat")            { p.dump_slat = true; }
        else if (!a.empty() && a[0] == '-')     { fprintf(stderr, "[trellis] unknown option: %s\n", a.c_str()); return false; }
        else if (positional == 0)               { p.image  = a; positional = 1; }
        else if (positional == 1)               { p.output = a; positional = 2; }
        else                                    { fprintf(stderr, "[trellis] unexpected argument: %s\n", a.c_str()); return false; }
    }
    std::string validation_error;
    if (!validate_sampler_params(p, validation_error) ||
        !validate_density_params(p, validation_error)) {
        fprintf(stderr, "[trellis] %s\n", validation_error.c_str());
        return false;
    }
    return true;
}

}  // namespace trellis
