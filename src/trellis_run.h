#pragma once
#include "trellis_args.h"

namespace trellis {
inline constexpr int kRunSuccess = 0;
inline constexpr int kRunInvalidParams = 2;
inline constexpr int kRunCascadeDensityExceeded = 3;
}  // namespace trellis

// Run the full TRELLIS.2 image->3D pipeline described by `p`: reconstruct
// `p.image` into a textured GLB at `p.output`. Returns 0 on success. All
// behavior (resolution, bg removal, texture, guidance, ...) comes from `p`,
// which trellis-cli / trellis-server populate via trellis::parse_args.
int trellis_run(const trellis::TrellisParams& p);
