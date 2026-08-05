#include "simplify_modes.h"
#include "meshoptimizer.h"

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace trellis {
namespace {

size_t simplify_once(std::vector<unsigned int>& output,
                     const std::vector<unsigned int>& input,
                     const std::vector<float>& verts, int V,
                     size_t target_indices, float error_fraction,
                     float& result_error) {
    output.resize(input.size());
    return meshopt_simplify(
        output.data(), input.data(), input.size(),
        verts.data(), static_cast<size_t>(V), 3 * sizeof(float),
        target_indices, error_fraction, 0, &result_error);
}

struct Candidate {
    std::vector<unsigned int> indices;
    size_t count = 0;
    float error = 0.0f;
    size_t first_attempt_count = 0;
    bool floor_adjusted = false;
};

Candidate simplify_candidate(const std::vector<unsigned int>& input,
                             const std::vector<float>& verts, int V,
                             size_t target_faces, float error_fraction) {
    Candidate result;
    result.count = simplify_once(
        result.indices, input, verts, V,
        std::min(input.size(), target_faces * 3),
        error_fraction, result.error);
    result.first_attempt_count = result.count;
    return result;
}

Candidate simplify_with_floor(const std::vector<unsigned int>& input,
                              const std::vector<float>& verts, int V,
                              size_t target_faces, size_t floor_faces,
                              float error_fraction) {
    const size_t input_faces = input.size() / 3;
    Candidate first = simplify_candidate(
        input, verts, V, target_faces, error_fraction);
    if (first.count / 3 >= floor_faces) return first;

    // meshoptimizer collapses edges in batches and can cross a face-count
    // target. Raise the internal target from the original mesh until a safe
    // result is found, then binary-search the bracket. Keep the lowest tested
    // result at or above the requested floor; the original is always safe.
    Candidate best;
    best.indices = input;
    best.count = input.size();
    best.error = 0.0f;

    size_t bad_target = target_faces;
    size_t good_target = input_faces;
    size_t delta = std::max<size_t>(
        1, floor_faces - first.count / 3);
    bool bracketed = false;
    while (bad_target < input_faces) {
        const size_t next_target =
            std::min(input_faces, bad_target + delta);
        Candidate next = simplify_candidate(
            input, verts, V, next_target, error_fraction);
        if (next.count / 3 >= floor_faces) {
            good_target = next_target;
            if (next.count < best.count) best = std::move(next);
            bracketed = true;
            break;
        }
        bad_target = next_target;
        if (bad_target == input_faces) break;
        delta = delta > input_faces / 2 ? input_faces : delta * 2;
    }
    if (!bracketed) {
        best.first_attempt_count = first.count;
        best.floor_adjusted = true;
        return best;
    }

    while (bad_target + 1 < good_target) {
        const size_t mid = bad_target + (good_target - bad_target) / 2;
        Candidate next = simplify_candidate(
            input, verts, V, mid, error_fraction);
        if (next.count / 3 >= floor_faces) {
            good_target = mid;
            if (next.count < best.count) best = std::move(next);
        } else {
            bad_target = mid;
        }
    }
    best.first_attempt_count = first.count;
    best.floor_adjusted = true;
    return best;
}

bool compact_mesh_exact(const std::vector<float>& verts, int V,
                        const std::vector<unsigned int>& indices, size_t count,
                        std::vector<float>& out_verts,
                        std::vector<int32_t>& out_faces) {
    if (count > indices.size() || count % 3 != 0) return false;
    for (size_t i = 0; i < count; ++i)
        if (indices[i] >= static_cast<unsigned int>(V)) return false;

    std::vector<int> remap(static_cast<size_t>(V), -1);
    out_verts.clear();
    out_faces.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const unsigned int vertex = indices[i];
        int& mapped = remap[vertex];
        if (mapped < 0) {
            mapped = static_cast<int>(out_verts.size() / 3);
            out_verts.insert(out_verts.end(),
                             &verts[3 * vertex], &verts[3 * vertex] + 3);
        }
        out_faces[i] = mapped;
    }
    return true;
}

} // namespace

MeshoptAdaptiveReport decimate_meshopt_adaptive(
    const std::vector<float>& verts, int V,
    const std::vector<int32_t>& faces, int F,
    const MeshoptAdaptiveOptions& options,
    std::vector<float>& out_verts,
    std::vector<int32_t>& out_faces) {
    MeshoptAdaptiveReport report;
    report.input_faces = F;
    report.requested_error_percent = options.max_error_percent;

    assert(V >= 0 && F >= 0);
    assert(verts.size() == static_cast<size_t>(V) * 3);
    assert(faces.size() == static_cast<size_t>(F) * 3);
    assert(std::isfinite(options.max_error_percent));
    assert(options.max_error_percent >= 0.0f && options.max_error_percent <= 100.0f);
    if (options.mode == MeshoptAdaptiveMode::Bounded) {
        assert(options.min_faces >= 1);
        assert(options.max_faces >= options.min_faces);
        assert(options.min_faces <= F);
    }

    std::vector<unsigned int> input(faces.begin(), faces.end());
    const float error_fraction = options.max_error_percent / 100.0f;
    Candidate quality = options.mode == MeshoptAdaptiveMode::ErrorOnly
        ? simplify_candidate(input, verts, V, 0, error_fraction)
        : simplify_with_floor(
              input, verts, V,
              static_cast<size_t>(options.min_faces),
              static_cast<size_t>(options.min_faces),
              error_fraction);
    report.quality_faces = static_cast<int>(quality.count / 3);
    Candidate output = std::move(quality);
    bool error_limited = false;

    if (options.mode == MeshoptAdaptiveMode::ErrorOnly &&
        output.count > 0) {
        Candidate unlimited = simplify_candidate(
            input, verts, V, 0, FLT_MAX);
        error_limited = unlimited.count < output.count;
    }

    if (options.mode == MeshoptAdaptiveMode::Bounded &&
        report.quality_faces > options.max_faces) {
        Candidate forced = simplify_with_floor(
            input, verts, V,
            static_cast<size_t>(options.max_faces),
            static_cast<size_t>(options.min_faces),
            FLT_MAX);
        report.forced_attempt_faces =
            static_cast<int>(forced.first_attempt_count / 3);
        if (forced.count / 3 >= static_cast<size_t>(options.min_faces) &&
            forced.count < output.count) {
            output = std::move(forced);
            report.forced = true;
        }
    }

    bool invalid_result = !compact_mesh_exact(
        verts, V, output.indices, output.count, out_verts, out_faces);
    if (invalid_result) {
        output.indices = input;
        output.count = input.size();
        output.error = 0.0f;
        report.forced = false;
        (void)compact_mesh_exact(
            verts, V, output.indices, output.count, out_verts, out_faces);
    }
    report.output_faces = static_cast<int>(out_faces.size() / 3);
    report.result_error_percent = output.error * 100.0f;
    report.mesh_extent = meshopt_simplifyScale(
        verts.data(), static_cast<size_t>(V), 3 * sizeof(float));
    report.result_error_units = output.error * report.mesh_extent;
    report.no_progress = report.output_faces >= report.input_faces;
    report.min_met = options.mode != MeshoptAdaptiveMode::Bounded ||
        report.output_faces >= options.min_faces;
    report.met_max = options.mode != MeshoptAdaptiveMode::Bounded ||
        report.output_faces <= options.max_faces;
    report.error_met =
        report.result_error_percent <= options.max_error_percent + 1e-5f;

    if (invalid_result) {
        report.stop_reason = "invalid-index";
    } else if (options.mode == MeshoptAdaptiveMode::ErrorOnly) {
        if (report.no_progress)
            report.stop_reason = error_limited
                ? "no-progress-error-limit"
                : "no-progress-topology-limit";
        else
            report.stop_reason =
                error_limited ? "error-limit" : "topology-limit";
    } else if (!report.met_max) {
        report.stop_reason = "band-unreachable";
    } else if (report.forced) {
        report.stop_reason = "max-forced";
    } else if (report.no_progress) {
        report.stop_reason = "no-progress";
    } else if (report.output_faces <= options.min_faces) {
        report.stop_reason = "min-reached";
    } else {
        report.stop_reason = "quality-stop";
    }

    std::printf("[meshopt-policy] mode=%s input_faces=%d min_faces=",
                options.mode == MeshoptAdaptiveMode::ErrorOnly ? "error" : "range",
                report.input_faces);
    if (options.mode == MeshoptAdaptiveMode::ErrorOnly) std::printf("none");
    else std::printf("%d", options.min_faces);
    std::printf(" max_faces=");
    if (options.mode == MeshoptAdaptiveMode::ErrorOnly) std::printf("none");
    else std::printf("%d", options.max_faces);
    std::printf(
        " requested_error_percent=%.6g result_error_percent=%.6g "
        "extent=%.6g result_error_units=%.6g quality_faces=%d output_faces=%d "
        "stop_reason=%s min_met=%s max_met=%s error_met=%s forced=%s "
        "forced_attempt_faces=",
        report.requested_error_percent, report.result_error_percent,
        report.mesh_extent, report.result_error_units,
        report.quality_faces, report.output_faces, report.stop_reason,
        options.mode == MeshoptAdaptiveMode::ErrorOnly ? "na" :
            (report.min_met ? "yes" : "no"),
        options.mode == MeshoptAdaptiveMode::ErrorOnly ? "na" :
            (report.met_max ? "yes" : "no"),
        report.error_met ? "yes" : "no",
        report.forced ? "yes" : "no");
    if (report.forced_attempt_faces < 0) std::printf("none\n");
    else std::printf("%d\n", report.forced_attempt_faces);
    std::fflush(stdout);
    return report;
}

} // namespace trellis
