#include "trellis_args.h"

#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
int failures = 0;

#define CHECK(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
    ++failures; \
} } while (0)

bool parse(std::initializer_list<const char*> values, trellis::TrellisParams& p) {
    std::vector<std::string> storage(values.begin(), values.end());
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (std::string& value : storage) argv.push_back(value.data());
    return trellis::parse_args(static_cast<int>(argv.size()), argv.data(), p);
}
}  // namespace

int main() {
    std::string error;
    {
        trellis::TrellisParams p;
        CHECK(p.gss == 7.5f && p.gsh == 7.5f);
        CHECK(p.sparse_steps == 12 && p.sparse_guidance_rescale == 0.7f);
        CHECK(p.sparse_guidance_interval_start == 0.6f);
        CHECK(p.sparse_guidance_interval_end == 1.0f && p.sparse_rescale_t == 5.0f);
        CHECK(p.shape_steps == 12 && p.shape_guidance_rescale == 0.5f);
        CHECK(p.shape_guidance_interval_start == 0.6f);
        CHECK(p.shape_guidance_interval_end == 1.0f && p.shape_rescale_t == 3.0f);
        CHECK(p.texture_steps == 12 && p.texture_guidance_strength == 1.0f);
        CHECK(p.texture_guidance_rescale == 0.0f);
        CHECK(p.texture_guidance_interval_start == 0.6f);
        CHECK(p.texture_guidance_interval_end == 0.9f && p.texture_rescale_t == 3.0f);
        CHECK(trellis::validate_sampler_params(p, error));
    }
    {
        trellis::TrellisParams p;
        p.gss = 6.25f;
        p.gsh = 8.5f;
        CHECK(trellis::validate_sampler_params(p, error));

        struct Override { const char* name; const char* value; };
        const Override overrides[] = {
            {"--sparse-steps", "20"},
            {"--sparse-guidance", "7"},
            {"sparse_guidance_rescale", "0.25"},
            {"--sparse-guidance-start", "0.1"},
            {"sparse_guidance_interval_end", "0.8"},
            {"--sparse-rescale-t", "4"},
            {"shape_steps", "18"},
            {"shape_guidance_strength", "9"},
            {"--shape-guidance-rescale", "0.4"},
            {"shape_guidance_interval_start", "0.2"},
            {"--shape-guidance-end", "0.7"},
            {"shape_rescale_t", "2.5"},
            {"--texture-steps", "16"},
            {"--texture-guidance", "1.25"},
            {"texture_guidance_rescale", "0.1"},
            {"--texture-guidance-interval-start", "0.25"},
            {"texture_guidance_interval_end", "0.75"},
            {"--texture-rescale-t", "2"},
        };
        for (const Override& item : overrides)
            CHECK(trellis::set_sampler_option(item.name, item.value, p, error));
        CHECK(trellis::validate_sampler_params(p, error));
        CHECK(p.sparse_steps == 20 && p.gss == 7.0f && p.sparse_rescale_t == 4.0f);
        CHECK(p.shape_steps == 18 && p.gsh == 9.0f && p.shape_rescale_t == 2.5f);
        CHECK(p.texture_steps == 16 && p.texture_guidance_strength == 1.25f);
        CHECK(p.texture_guidance_interval_start == 0.25f);
        CHECK(trellis::is_sampler_option("--gss"));
        CHECK(trellis::is_sampler_option("gsh"));
        CHECK(!trellis::is_sampler_option("--sparse-unknown"));
    }
    {
        trellis::TrellisParams p;
        const int old_steps = p.sparse_steps;
        const float old_guidance = p.gss;
        CHECK(!trellis::set_sampler_option("--sparse-steps", "12junk", p, error));
        CHECK(p.sparse_steps == old_steps);
        CHECK(!trellis::set_sampler_option("--sparse-steps", "", p, error));
        CHECK(!trellis::set_sampler_option("--sparse-steps", " 12", p, error));
        CHECK(!trellis::set_sampler_option("--sparse-steps", "12 ", p, error));
        CHECK(!trellis::set_sampler_option("--sparse-steps", "999999999999999999", p, error));
        CHECK(!trellis::set_sampler_option("--gss", "nan", p, error));
        CHECK(!trellis::set_sampler_option("--gss", "inf", p, error));
        CHECK(!trellis::set_sampler_option("--gss", "1e999", p, error));
        CHECK(p.gss == old_guidance);
    }
    {
        trellis::TrellisParams p;
        CHECK(parse({"test", "--shape-guidance-end", "0.2",
                     "--shape-guidance-start", "0.1",
                     "--sparse-steps", "20", "--sparse-guidance", "5.5",
                     "--gss", "6.5", "--texture-guidance", "1.25"}, p));
        CHECK(p.shape_guidance_interval_start == 0.1f);
        CHECK(p.shape_guidance_interval_end == 0.2f);
        CHECK(p.sparse_steps == 20 && p.gss == 6.5f);
        CHECK(p.texture_guidance_strength == 1.25f);

        trellis::TrellisParams bad;
        CHECK(!parse({"test", "--sparse-steps", "12junk"}, bad));
    }
    {
        trellis::TrellisParams p;
        p.sparse_steps = 0;
        CHECK(!trellis::validate_sampler_params(p, error));
        p = trellis::TrellisParams{};
        p.shape_steps = 1001;
        CHECK(!trellis::validate_sampler_params(p, error));
        p = trellis::TrellisParams{};
        p.gss = -1.0f;
        CHECK(!trellis::validate_sampler_params(p, error));
        p = trellis::TrellisParams{};
        p.texture_guidance_rescale = -0.1f;
        CHECK(!trellis::validate_sampler_params(p, error));
        p = trellis::TrellisParams{};
        p.texture_guidance_rescale = 1.1f;
        CHECK(!trellis::validate_sampler_params(p, error));
        p = trellis::TrellisParams{};
        p.shape_guidance_interval_start = 0.9f;
        p.shape_guidance_interval_end = 0.8f;
        CHECK(!trellis::validate_sampler_params(p, error));
        p = trellis::TrellisParams{};
        p.texture_guidance_interval_end = 1.1f;
        CHECK(!trellis::validate_sampler_params(p, error));
        p = trellis::TrellisParams{};
        p.sparse_rescale_t = 0.0f;
        CHECK(!trellis::validate_sampler_params(p, error));
    }

    if (failures) return 1;
    std::puts("sampler argument tests passed");
    return 0;
}
