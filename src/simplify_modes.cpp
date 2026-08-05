#include "simplify_modes.h"
#include "meshoptimizer.h"

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
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

void compact_mesh(const std::vector<float>& verts, int V,
                  const std::vector<unsigned int>& indices, size_t count,
                  std::vector<float>& out_verts,
                  std::vector<int32_t>& out_faces) {
    std::vector<int> remap(static_cast<size_t>(V), -1);
    out_verts.clear();
    out_faces.clear();
    out_faces.reserve(count);

    for (size_t i = 0; i + 2 < count; i += 3) {
        const unsigned int a = indices[i];
        const unsigned int b = indices[i + 1];
        const unsigned int c = indices[i + 2];
        if (a >= static_cast<unsigned int>(V) ||
            b >= static_cast<unsigned int>(V) ||
            c >= static_cast<unsigned int>(V))
            continue;
        if (a == b || b == c || a == c) continue;

        const float abx = verts[3 * b] - verts[3 * a];
        const float aby = verts[3 * b + 1] - verts[3 * a + 1];
        const float abz = verts[3 * b + 2] - verts[3 * a + 2];
        const float acx = verts[3 * c] - verts[3 * a];
        const float acy = verts[3 * c + 1] - verts[3 * a + 1];
        const float acz = verts[3 * c + 2] - verts[3 * a + 2];
        const float cx = aby * acz - abz * acy;
        const float cy = abz * acx - abx * acz;
        const float cz = abx * acy - aby * acx;
        if (cx * cx + cy * cy + cz * cz <= 0.0f) continue;

        const unsigned int tri[3] = {a, b, c};
        for (unsigned int vertex : tri) {
            assert(vertex < static_cast<unsigned int>(V));
            int& mapped = remap[vertex];
            if (mapped < 0) {
                mapped = static_cast<int>(out_verts.size() / 3);
                out_verts.insert(out_verts.end(),
                                 &verts[3 * vertex], &verts[3 * vertex] + 3);
            }
            out_faces.push_back(mapped);
        }
    }
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
    }

    std::vector<unsigned int> input(faces.begin(), faces.end());
    std::vector<unsigned int> candidate;
    const float error_fraction = options.max_error_percent / 100.0f;
    const size_t target_indices = options.mode == MeshoptAdaptiveMode::ErrorOnly
        ? 0
        : std::min(input.size(), static_cast<size_t>(options.min_faces) * 3);

    float result_error = 0.0f;
    size_t result_count = simplify_once(candidate, input, verts, V,
                                        target_indices, error_fraction,
                                        result_error);
    report.quality_faces = static_cast<int>(result_count / 3);

    if (options.mode == MeshoptAdaptiveMode::Bounded &&
        report.quality_faces > options.max_faces) {
        std::vector<unsigned int> forced_indices;
        float forced_error = 0.0f;
        const size_t forced_count = simplify_once(
            forced_indices, input, verts, V,
            std::min(input.size(), static_cast<size_t>(options.max_faces) * 3),
            FLT_MAX, forced_error);
        // Protect the floor even if a future meshoptimizer version can
        // overshoot a count target. Keep the quality candidate when topology
        // prevents the forced pass from making additional progress.
        if (forced_count / 3 >= static_cast<size_t>(options.min_faces) &&
            forced_count < result_count) {
            candidate.swap(forced_indices);
            result_count = forced_count;
            result_error = forced_error;
            report.forced = true;
        }
    }

    compact_mesh(verts, V, candidate, result_count, out_verts, out_faces);
    report.output_faces = static_cast<int>(out_faces.size() / 3);
    report.result_error_percent = result_error * 100.0f;
    report.mesh_extent = meshopt_simplifyScale(
        verts.data(), static_cast<size_t>(V), 3 * sizeof(float));
    report.result_error_units = result_error * report.mesh_extent;
    report.no_progress = report.output_faces >= report.input_faces;
    report.min_met = options.mode != MeshoptAdaptiveMode::Bounded ||
        report.output_faces >= options.min_faces;
    report.met_max = options.mode != MeshoptAdaptiveMode::Bounded ||
        report.output_faces <= options.max_faces;
    report.error_met =
        report.result_error_percent <= options.max_error_percent + 1e-5f;

    if (options.mode == MeshoptAdaptiveMode::ErrorOnly) {
        report.stop_reason =
            report.no_progress ? "no-progress" : "error-or-topology";
    } else if (!report.min_met) {
        report.stop_reason = "min-missed";
    } else if (!report.met_max) {
        report.stop_reason = "max-missed";
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
        "stop_reason=%s min_met=%s max_met=%s error_met=%s forced=%s\n",
        report.requested_error_percent, report.result_error_percent,
        report.mesh_extent, report.result_error_units,
        report.quality_faces, report.output_faces, report.stop_reason,
        options.mode == MeshoptAdaptiveMode::ErrorOnly ? "na" :
            (report.min_met ? "yes" : "no"),
        options.mode == MeshoptAdaptiveMode::ErrorOnly ? "na" :
            (report.met_max ? "yes" : "no"),
        report.error_met ? "yes" : "no",
        report.forced ? "yes" : "no");
    std::fflush(stdout);
    return report;
}

} // namespace trellis
