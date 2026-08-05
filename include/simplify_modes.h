#pragma once

#include <cstdint>
#include <vector>

namespace trellis {

// meshoptimizer-native adaptive modes. Fixed-count QEM and fixed-count
// meshoptimizer remain in uv_bake.h so their existing behaviour is unchanged.
enum class MeshoptAdaptiveMode {
    ErrorOnly,
    Bounded,
};

struct MeshoptAdaptiveOptions {
    MeshoptAdaptiveMode mode = MeshoptAdaptiveMode::ErrorOnly;
    // Bounded mode never intentionally goes below min_faces. It first computes
    // the quality result under the error budget, then explicitly forces toward
    // max_faces if needed while retaining both counts in the report.
    int min_faces = 0;
    int max_faces = 0;
    float max_error_percent = 1.0f;
};

struct MeshoptAdaptiveReport {
    int input_faces = 0;
    int quality_faces = 0;
    int output_faces = 0;
    float requested_error_percent = 0.0f;
    float result_error_percent = 0.0f;
    float mesh_extent = 0.0f;
    float result_error_units = 0.0f;
    bool forced = false;
    bool min_met = true;
    bool met_max = true;
    bool error_met = true;
    bool no_progress = false;
    const char* stop_reason = "unknown";
};

// ErrorOnly: target count is zero, so meshoptimizer keeps removing the
// cheapest legal collapses until max_error_percent or topology stops it.
//
// Bounded: first compute the quality result toward min_faces under the error
// limit. If it remains above max_faces, explicitly re-run from the original
// mesh to max_faces without an error cap. The report retains quality_faces and
// marks forced/error_met so the count-vs-quality tradeoff is never silent.
MeshoptAdaptiveReport decimate_meshopt_adaptive(
    const std::vector<float>& verts, int V,
    const std::vector<int32_t>& faces, int F,
    const MeshoptAdaptiveOptions& options,
    std::vector<float>& out_verts,
    std::vector<int32_t>& out_faces);

} // namespace trellis
