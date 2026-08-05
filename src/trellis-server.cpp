// trellis-server — resident HTTP wrapper around the TRELLIS.2 image->3D pipeline.
//
//   GET  /health     -> "ok"
//   POST /generate    multipart/form-data with an "image" file part; optional text
//                      fields "seed", "resolution" (512/1024/1536), "bg_removal"
//                      (threshold|birefnet), "uv" (xatlas = default, unique
//                      chart space; box = faster projection), "band" (narrow-band
//                      DC remesh band width), per-stage sampler overrides, and
//                      "c2s_diagnostics" (on/off). Returns model/gltf-binary.
//
// Launch-time defaults come from CLI flags (see trellis::parse_args);
// each request copies those defaults and applies its own overrides. The model
// directory is resolved once; each request runs the full pipeline via trellis_run()
// (per-stage load/free, like trellis-cli), serialized by a mutex. Keeping the
// process resident avoids re-initializing the Vulkan backend on every request.
#include "trellis_args.h"
#include "trellis_run.h"
#include "httplib.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace {

std::string read_file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool write_file_bytes(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(data.data(), (std::streamsize) data.size());
    return f.good();
}

std::string json_escape(const std::string& value) {
    std::string escaped;
    for (char c : value) {
        switch (c) {
            case '"':  escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n";  break;
            case '\r': escaped += "\\r";  break;
            case '\t': escaped += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20) escaped += c;
        }
    }
    return escaped;
}

void set_json_error(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content("{\"error\":\"" + json_escape(message) + "\"}", "application/json");
}

constexpr const char* kSamplerFields[] = {
    "sparse_steps", "sparse_guidance_strength", "sparse_guidance_rescale",
    "sparse_guidance_interval_start", "sparse_guidance_interval_end", "sparse_rescale_t",
    "shape_steps", "shape_guidance_strength", "shape_guidance_rescale",
    "shape_guidance_interval_start", "shape_guidance_interval_end", "shape_rescale_t",
    "texture_steps", "texture_guidance_strength", "texture_guidance_rescale",
    "texture_guidance_interval_start", "texture_guidance_interval_end", "texture_rescale_t",
    "gss", "gsh",
};

// std::tmpnam on MSVC yields drive-root paths ("\sXXX.N") that a non-elevated
// process cannot write; stage scratch files in the real temp directory instead.
std::string temp_stem() {
    static std::atomic<unsigned> counter{0};
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec) dir = ".";
    auto n = counter.fetch_add(1);
    return (dir / ("trellis-req-" + std::to_string(n))).string();
}

}  // namespace

int main(int argc, char** argv) {
    // Stage progress goes to stdout, which is fully buffered when piped (e.g.
    // under Lemonade's output capture) — keep it line-visible for diagnostics.
    setvbuf(stdout, nullptr, _IONBF, 0);

    trellis::TrellisParams base;
    if (!trellis::parse_args(argc, argv, base)) {
        trellis::print_usage(argv[0], /*server=*/true);
        return base.help ? 0 : 1;
    }

    std::mutex gen_mu;
    httplib::Server svr;

    // Trellis Studio (and any browser client) calls this server from a different
    // origin — a Tauri webview is tauri://localhost / http://tauri.localhost, and a
    // browser-served UI is another port — so every response needs permissive CORS
    // headers, and a multipart POST with non-simple headers may be preflighted with
    // OPTIONS. Applied to every route via the post-routing hook + a catch-all OPTIONS.
    svr.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.set_header("Access-Control-Max-Age", "86400");
    });
    svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;  // headers added by the post-routing handler above
    });

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("ok", "text/plain");
    });

    svr.Post("/generate", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("image")) {
            res.status = 400;
            res.set_content("{\"error\":\"missing 'image' file part\"}", "application/json");
            return;
        }
        const auto& image = req.get_file_value("image");

        // Per-request params start from the launch defaults, then apply overrides.
        trellis::TrellisParams p = base;
        if (req.has_file("seed")) p.seed = (uint32_t) atoi(req.get_file_value("seed").content.c_str());
        if (req.has_file("resolution")) p.set_res(atoi(req.get_file_value("resolution").content.c_str()));
        if (req.has_file("bg_removal")) p.birefnet = (req.get_file_value("bg_removal").content == "birefnet") ? 1 : 0;
        if (req.has_file("uv")) p.xatlas = (req.get_file_value("uv").content == "xatlas");
        if (req.has_file("band")) p.band = atoi(req.get_file_value("band").content.c_str());
        if (req.has_file("webp")) {
            const std::string& w = req.get_file_value("webp").content;
            p.webp = (w == "off" || w == "0" || w == "false") ? 0
                   : (w == "on"  || w == "1" || w == "true")  ? 1 : -1;
        }

        std::string sampler_error;
        for (const char* field : kSamplerFields) {
            if (!req.has_file(field)) continue;
            if (!trellis::set_sampler_option(
                    field, req.get_file_value(field).content, p, sampler_error)) {
                set_json_error(res, 400, sampler_error);
                return;
            }
        }
        if (!trellis::validate_sampler_params(p, sampler_error)) {
            set_json_error(res, 400, sampler_error);
            return;
        }
        if (req.has_file("c2s_diagnostics")) {
            const std::string& value = req.get_file_value("c2s_diagnostics").content;
            if (value == "on" || value == "1" || value == "true") {
                p.c2s_diagnostics = true;
            } else if (value == "off" || value == "0" || value == "false") {
                p.c2s_diagnostics = false;
            } else {
                set_json_error(
                    res, 400,
                    "invalid c2s_diagnostics; use on/off, true/false, or 1/0");
                return;
            }
        }

        const std::string stem = temp_stem();
        p.image  = stem + ".png";
        p.output = stem + ".glb";

        std::string glb;
        std::string error_message = "3D reconstruction failed";
        {
            std::lock_guard<std::mutex> lk(gen_mu);
            if (!write_file_bytes(p.image, image.content)) {
                res.status = 500;
                res.set_content("{\"error\":\"failed to stage input image\"}", "application/json");
                return;
            }
            fprintf(stderr, "[trellis-server] generate: %zu-byte image, seed %u, res %s, bg %s, uv %s\n",
                    image.content.size(), p.seed, p.cascade ? std::to_string(p.hr_res).c_str() : "512",
                    p.birefnet < 0 ? "auto" : (p.birefnet ? "birefnet" : "threshold"), p.xatlas ? "xatlas" : "box");
            try {
                int rc = trellis_run(p);
                if (rc == 0) glb = read_file_bytes(p.output);
            } catch (const std::exception& e) {
                fprintf(stderr, "[trellis-server] generate failed: %s\n", e.what());
                error_message = e.what();
            }
            std::remove(p.image.c_str());
            std::remove(p.output.c_str());
            // trellis_run also writes sibling debug artifacts; clean them up too.
            std::remove((stem + ".ply").c_str());
            std::remove((stem + "_base.png").c_str());
        }

        if (glb.empty()) {
            set_json_error(res, 500, error_message);
            return;
        }
        res.set_content(glb.data(), glb.size(), "model/gltf-binary");
    });

    fprintf(stderr, "[trellis-server] models=%s gpu=%d listening on http://%s:%d\n",
            base.models.c_str(), base.gpu, base.host.c_str(), base.port);
    if (!svr.listen(base.host, base.port)) {
        fprintf(stderr, "[trellis-server] failed to bind %s:%d\n", base.host.c_str(), base.port);
        return 1;
    }
    return 0;
}
