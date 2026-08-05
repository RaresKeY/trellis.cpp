#include "simplify_modes.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                     __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

void make_grid(int cells, bool bump,
               std::vector<float>& verts,
               std::vector<int32_t>& faces) {
    const int side = cells + 1;
    for (int y = 0; y <= cells; ++y) {
        for (int x = 0; x <= cells; ++x) {
            const float fx = static_cast<float>(x) / cells;
            const float fy = static_cast<float>(y) / cells;
            const float z = bump
                ? 0.03f * std::sin(fx * 6.2831853f) *
                      std::sin(fy * 6.2831853f)
                : 0.0f;
            verts.insert(verts.end(), {fx, fy, z});
        }
    }
    for (int y = 0; y < cells; ++y) {
        for (int x = 0; x < cells; ++x) {
            const int a = y * side + x;
            const int b = a + 1;
            const int c = a + side;
            const int d = c + 1;
            faces.insert(faces.end(), {a, b, d, a, d, c});
        }
    }
}

} // namespace

int main() {
    std::vector<float> flat_vertices;
    std::vector<int32_t> flat_faces;
    make_grid(20, false, flat_vertices, flat_faces);

    trellis::MeshoptAdaptiveOptions error;
    error.mode = trellis::MeshoptAdaptiveMode::ErrorOnly;
    error.max_error_percent = 0.01f;
    std::vector<float> out_vertices;
    std::vector<int32_t> out_faces;
    const auto flat = trellis::decimate_meshopt_adaptive(
        flat_vertices, static_cast<int>(flat_vertices.size() / 3),
        flat_faces, static_cast<int>(flat_faces.size() / 3),
        error, out_vertices, out_faces);
    CHECK(flat.output_faces < flat.input_faces);
    CHECK(flat.result_error_percent <= error.max_error_percent + 1e-4f);

    error.max_error_percent = 0.0f;
    const auto flat_zero = trellis::decimate_meshopt_adaptive(
        flat_vertices, static_cast<int>(flat_vertices.size() / 3),
        flat_faces, static_cast<int>(flat_faces.size() / 3),
        error, out_vertices, out_faces);
    CHECK(flat_zero.output_faces < flat_zero.input_faces);
    CHECK(flat_zero.result_error_percent == 0.0f);

    std::vector<float> bump_vertices;
    std::vector<int32_t> bump_faces;
    make_grid(20, true, bump_vertices, bump_faces);
    error.max_error_percent = 0.001f;
    const auto bump = trellis::decimate_meshopt_adaptive(
        bump_vertices, static_cast<int>(bump_vertices.size() / 3),
        bump_faces, static_cast<int>(bump_faces.size() / 3),
        error, out_vertices, out_faces);
    CHECK(bump.output_faces > 0);
    CHECK(bump.result_error_percent <= error.max_error_percent + 1e-4f);

    trellis::MeshoptAdaptiveOptions bounded;
    bounded.mode = trellis::MeshoptAdaptiveMode::Bounded;
    bounded.min_faces = 100;
    bounded.max_faces = 200;
    bounded.max_error_percent = 0.1f;
    const auto range = trellis::decimate_meshopt_adaptive(
        bump_vertices, static_cast<int>(bump_vertices.size() / 3),
        bump_faces, static_cast<int>(bump_faces.size() / 3),
        bounded, out_vertices, out_faces);
    CHECK(range.output_faces >= bounded.min_faces);
    CHECK(range.met_max == (range.output_faces <= bounded.max_faces));
    CHECK(range.min_met);
    CHECK(range.quality_faces >= range.output_faces);
    if (range.forced) CHECK(!range.error_met);

    std::puts("simplify mode tests passed");
    return 0;
}
