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

    {
        trellis::TrellisParams defaults;
        CHECK(!defaults.c2s_diagnostics);
        CHECK(parse({"test"}, defaults));
        CHECK(!defaults.c2s_diagnostics);

        trellis::TrellisParams enabled;
        CHECK(parse({"test", "--c2s-diagnostics"}, enabled));
        CHECK(enabled.c2s_diagnostics);
    }

    {
        trellis::TrellisParams p;
        CHECK(p.cascade && !p.direct_1024 && p.hr_res == 1024);
        CHECK(std::string(trellis::pipeline_name(p)) == "1024-cascade");
        CHECK(trellis::validate_pipeline_params(p, error));

        CHECK(trellis::set_pipeline_option("1024-direct", p, error));
        CHECK(!p.cascade && p.direct_1024 && p.hr_res == 1024);
        CHECK(std::string(trellis::pipeline_name(p)) == "1024-direct");
        CHECK(trellis::validate_pipeline_params(p, error));

        p.set_res(1024);
        CHECK(p.cascade && !p.direct_1024 && p.hr_res == 1024);
        CHECK(std::string(trellis::pipeline_name(p)) == "1024-cascade");

        CHECK(trellis::set_pipeline_option("1024", p, error));
        CHECK(p.direct_1024 && !p.cascade);
        CHECK(trellis::set_pipeline_option("1024_cascade", p, error));
        CHECK(p.cascade && !p.direct_1024);

        CHECK(trellis::set_resolution_option("512", p, error));
        CHECK(!p.cascade && !p.direct_1024 && p.hr_res == 512);
        CHECK(!trellis::set_resolution_option("1024junk", p, error));
        CHECK(!trellis::set_resolution_option(" 1024", p, error));
        CHECK(!trellis::set_resolution_option("1024 ", p, error));
        CHECK(!trellis::set_resolution_option("768", p, error));
        CHECK(!trellis::set_pipeline_option("1024-direct-junk", p, error));
    }
    {
        trellis::TrellisParams by_res;
        CHECK(parse({"test", "--res", "1024"}, by_res));
        CHECK(by_res.cascade && !by_res.direct_1024 && by_res.hr_res == 1024);

        trellis::TrellisParams direct;
        CHECK(parse({"test", "--pipeline", "1024-direct"}, direct));
        CHECK(!direct.cascade && direct.direct_1024 && direct.hr_res == 1024);

        trellis::TrellisParams conflict_a;
        CHECK(!parse({"test", "--res", "1024",
                      "--pipeline", "1024-direct"}, conflict_a));
        trellis::TrellisParams conflict_b;
        CHECK(!parse({"test", "--pipeline", "1024-direct",
                      "--res", "1024"}, conflict_b));
        trellis::TrellisParams bad_res;
        CHECK(!parse({"test", "--res", "1024junk"}, bad_res));
    }
    {
        trellis::TrellisParams invalid;
        invalid.direct_1024 = true;  // Contradictory with default cascade=true.
        CHECK(!trellis::validate_pipeline_params(invalid, error));

        trellis::TrellisParams direct;
        CHECK(trellis::set_pipeline_option("1024-direct", direct, error));
        direct.tex_res = 512;
        CHECK(!trellis::validate_pipeline_params(direct, error));
        direct.tex_res = 1024;
        CHECK(trellis::validate_pipeline_params(direct, error));
    }

    {
        trellis::TrellisParams p;
        CHECK(p.max_cascade_tokens == 0);
        CHECK(p.dense_policy == trellis::DensePolicy::Fallback512);
        CHECK(trellis::validate_density_params(p, error));

        CHECK(trellis::set_density_option(
            "--max-1024-tokens", "17000", p, error));
        CHECK(p.max_cascade_tokens == 17000);
        CHECK(trellis::set_density_option(
            "--max-cascade-tokens", "18000", p, error));
        CHECK(trellis::set_density_option(
            "dense_policy", "fail", p, error));
        CHECK(p.max_cascade_tokens == 18000);
        CHECK(p.dense_policy == trellis::DensePolicy::Fail);
        CHECK(trellis::validate_density_params(p, error));

        const int old_limit = p.max_cascade_tokens;
        CHECK(!trellis::set_density_option(
            "max_cascade_tokens", "12junk", p, error));
        CHECK(p.max_cascade_tokens == old_limit);
        CHECK(!trellis::set_density_option(
            "max_cascade_tokens", " 12", p, error));
        CHECK(!trellis::set_density_option(
            "max_cascade_tokens", "12 ", p, error));
        CHECK(!trellis::set_density_option(
            "max_cascade_tokens", "1.0", p, error));
        CHECK(!trellis::set_density_option(
            "max_cascade_tokens", "999999999999999999", p, error));
        CHECK(!trellis::set_density_option(
            "dense_policy", "best-effort", p, error));

        p = trellis::TrellisParams{};
        CHECK(trellis::set_density_option(
            "max_cascade_tokens", "-1", p, error));
        CHECK(!trellis::validate_density_params(p, error));
        p = trellis::TrellisParams{};
        p.dense_policy = static_cast<trellis::DensePolicy>(99);
        CHECK(!trellis::validate_density_params(p, error));

        trellis::TrellisParams parsed;
        CHECK(parse({"test", "--max-cascade-tokens", "18000",
                     "--dense-policy", "allow"}, parsed));
        CHECK(parsed.max_cascade_tokens == 18000);
        CHECK(parsed.dense_policy == trellis::DensePolicy::Allow);
        trellis::TrellisParams legacy;
        CHECK(parse({"test", "--max-1024-tokens", "17000"}, legacy));
        CHECK(legacy.max_cascade_tokens == 17000);

        trellis::TrellisParams invalid;
        CHECK(!parse({"test", "--max-cascade-tokens", "12junk"}, invalid));
    }
    {
        using trellis::DenseGuardAction;
        using trellis::DensePolicy;

        auto d = trellis::resolve_dense_guard(
            false, 1536, 0, 18000, DensePolicy::Fallback512);
        CHECK(d.action == DenseGuardAction::NotApplicable);
        CHECK(d.proceed && !d.resolved_cascade && d.resolved_resolution == 512);

        d = trellis::resolve_dense_guard(
            true, 1024, 999999, 0, DensePolicy::Fallback512);
        CHECK(d.action == DenseGuardAction::Disabled);
        CHECK(d.proceed && d.resolved_cascade && d.resolved_resolution == 1024);

        d = trellis::resolve_dense_guard(
            true, 1024, 17999, 18000, DensePolicy::Fail);
        CHECK(d.action == DenseGuardAction::Pass && d.proceed);
        d = trellis::resolve_dense_guard(
            true, 1024, 18000, 18000, DensePolicy::Fallback512);
        CHECK(d.action == DenseGuardAction::Pass && d.proceed);

        d = trellis::resolve_dense_guard(
            true, 1024, 18001, 18000, DensePolicy::Fail);
        CHECK(d.action == DenseGuardAction::Fail);
        CHECK(!d.proceed && d.resolved_cascade);

        d = trellis::resolve_dense_guard(
            true, 1024, 18001, 18000, DensePolicy::Fallback512);
        CHECK(d.action == DenseGuardAction::Fallback512);
        CHECK(d.proceed && !d.resolved_cascade && d.resolved_resolution == 512);

        d = trellis::resolve_dense_guard(
            true, 1536, 10001, 10000, DensePolicy::Allow);
        CHECK(d.action == DenseGuardAction::Allow);
        CHECK(d.proceed && d.resolved_cascade && d.resolved_resolution == 1536);
    }

    if (failures) return 1;
    std::puts("argument tests passed");
    return 0;
}
