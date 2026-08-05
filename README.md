# trellis.cpp

A standalone, **GGML-based C++** implementation of Microsoft's
[TRELLIS.2-4B](https://huggingface.co/microsoft/TRELLIS.2-4B) image-to-3D pipeline:
background removal, image conditioning, the three flow transformers, the VAE decoders,
mesh extraction and UV-textured GLB export — all in native C++/GGML with no Python at
runtime. Optionally driven end-to-end from a text prompt with
[stable-diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp) producing the
input image.

It ports the reference implementation at
[microsoft/TRELLIS.2](https://github.com/microsoft/TRELLIS.2).

```
./build/trellis-cli assets/goblin.png out/goblin.glb      # image -> UV-textured GLB (atlas + PBR)
python tools/render_glb.py out/goblin.glb out/view.png    # quick multi-view render
```

Prebuilt binaries for Linux and Windows (Vulkan, ROCm, CUDA) are published on the
[releases page](../../releases). Serves as the `trellis` backend of
[Lemonade](https://github.com/lemonade-sdk/lemonade).

## Quick start

New here? **Trellis Studio** is a one-command install that auto-detects your GPU
runtime (CUDA / ROCm / Vulkan), downloads the server + weights, and gives you a
drag-an-image → 3D desktop app with an interactive preview and a saved gallery.

```bash
# Linux (x86-64)
curl -fsSL https://raw.githubusercontent.com/pwilkin/trellis.cpp/main/install/install.sh | bash
```
```powershell
# Windows (x64), in PowerShell
irm https://raw.githubusercontent.com/pwilkin/trellis.cpp/main/install/install.ps1 | iex
```

See [**Trellis Studio (desktop app)**](#trellis-studio-desktop-app) below for what it
does and how to use it, or [`docs/getting-started.md`](docs/getting-started.md) for the
full walkthrough and installer options.

## Showcase

Seven image→3D reconstructions produced end-to-end by trellis.cpp **v0.4.3** on a
single Radeon 8060S (all res-1024 cascade, seed 42, ~300K faces, 2048² atlas; the
GLBs and their Z-Image-Turbo source images live in
[`assets/showcase/`](assets/showcase/)):

<p>
<a href="assets/showcase/axe/axe_quad4k.png"><img src="assets/showcase/axe/axe_quad4k.png" width="49%"></a>
<a href="assets/showcase/chest/chest_quad4k.png"><img src="assets/showcase/chest/chest_quad4k.png" width="49%"></a>
<a href="assets/showcase/cottage/cottage_quad4k.png"><img src="assets/showcase/cottage/cottage_quad4k.png" width="49%"></a>
<a href="assets/showcase/drone/drone_quad4k.png"><img src="assets/showcase/drone/drone_quad4k.png" width="49%"></a>
<a href="assets/showcase/golem/golem_quad4k.png"><img src="assets/showcase/golem/golem_quad4k.png" width="49%"></a>
<a href="assets/showcase/knight/knight_quad4k.png"><img src="assets/showcase/knight/knight_quad4k.png" width="49%"></a>
<a href="assets/showcase/racer/racer_quad4k.png"><img src="assets/showcase/racer/racer_quad4k.png" width="49%"></a>
</p>

Each grid is a 4096×4096 four-view capture — front / right / back / left at 75°
elevation, 2048² per view — made with
[`tools/mv_preview/render_quad.js`](tools/mv_preview/render_quad.js), a headless
Playwright driver around Google's `<model-viewer>`: it serves
`tools/mv_preview/quad.html`, loads the GLB into a 2×2 grid of viewers, waits for
auto-framing to settle, captures each view via `toBlob`, and
[`stitch_quad.py`](tools/mv_preview/stitch_quad.py) assembles the final grid.

## Trellis Studio (desktop app)

**Trellis Studio** is the standalone desktop app (built with [Tauri](https://tauri.app))
for anyone who wants image→3D without touching the command line. The one-command
installer above auto-detects your GPU runtime, downloads the matching
`trellis-server` build plus the ~16.5 GB of weights, and installs the app; on launch
it starts and supervises the server for you, so the whole flow is drag-image →
click → rotate the result.

<p align="center">
<img src="assets/trellis-studio.png" width="92%" alt="Trellis Studio — drop an image, generate a textured 3D model, preview it interactively">
</p>

**Using it:**

1. **Add an image** — drag-and-drop onto the drop zone, click to browse, or paste
   from the clipboard.
2. **Set options** (optional) — resolution (512 light / 1024 cascade / 1536 high),
   seed, background removal (auto / birefnet / threshold), and UV unwrap
   (xatlas / box). Defaults match the CLI.
3. **Generate 3D** — this takes a few minutes; a live stage line shows progress.
4. **Inspect** — rotate and zoom in the interactive preview (Google's three.js-based
   `<model-viewer>`); **Reset view** re-frames the camera and **Save GLB…** exports
   the model.
5. **Reuse** — every result is kept in a local **gallery** (IndexedDB): click a
   thumbnail to reload its model, input image, and settings — even after restarting
   the app.

The **Settings** panel (gear icon) points the app at a different models directory,
GPU index, or port; the models directory, backend, and server binary come from the
`config.json` the installer writes. Because the UI is a plain web bundle you can also
skip the app entirely and open it in a browser against a `trellis-server` you started
yourself — see [`docs/getting-started.md`](docs/getting-started.md) for that, the full
installer options, and troubleshooting. The app source lives in [`app/`](app/).

The sections below document the **CLI and HTTP server** directly, for advanced and
scripted use.

## Usage

The default is the **1024 cascade** (LR `flow_512` → upsample → HR `flow_1024` →
res-1024 decode, sharper geometry); `--res 512` selects the lighter res-512 path.
All behavior is driven by CLI flags — run `trellis-cli --help` for the full list.
The most useful ones:

| flag | effect |
|------|--------|
| `--res 512\|1024\|1536` | geometry resolution (512 = light path, no cascade) |
| `--max-cascade-tokens N` | optional post-backoff cascade token guard; `0` disables it (default) |
| `--dense-policy MODE` | action above the guard: `fail`, `fallback-512`, or `allow` |
| `--bg-removal threshold\|birefnet` | default **auto**: pre-matted images keep their alpha, otherwise the BiRefNet matte (~13s on GPU). The plain white-bg keyer cuts specular highlights out of the alpha — the flow then generates holes there — so it is opt-in only |
| `--no-texture` | geometry only |
| `--decim GRID` | legacy cluster-grid decimation (default: quadric simplify to 300K faces @1024 / 150K @512; `0` = keep the full-res mesh) |
| `--atlas PX` | UV atlas size (default 2048 @1024 / 1024 @512) |
| `--box-uv` | voxel-native 6-way box projection instead of the default xatlas unwrap (O(faces), faster, looser packing) |
| `--seed N` | RNG seed |
| `--require-gpu` | fail instead of falling back to the (very slow, RAM-hungry) CPU path |

### Sampler controls

The stock sampler schedule is unchanged unless one of these options is set:

| stage | steps | guidance | guidance rescale | guidance interval | time rescale |
|---|---:|---:|---:|---:|---:|
| sparse structure | 12 | 7.5 | 0.7 | 0.6–1.0 | 5.0 |
| shape SLAT | 12 | 7.5 | 0.5 | 0.6–1.0 | 3.0 |
| texture SLAT | 12 | 1.0 | 0.0 | 0.6–0.9 | 3.0 |

Each stage accepts `--<stage>-steps`, `--<stage>-guidance`,
`--<stage>-guidance-rescale`, `--<stage>-guidance-start`,
`--<stage>-guidance-end`, and `--<stage>-rescale-t`. `--gss` and `--gsh`
remain aliases for sparse and shape guidance, and the existing
`TrellisParams::gss` / `TrellisParams::gsh` source fields remain supported.
Shape controls apply to both the low-resolution and cascade high-resolution
shape passes.

Steps must be in 1–1000. Guidance must be finite and non-negative; guidance
rescale must be finite and in `[0,1]`; interval endpoints must satisfy
`0 <= start <= end <= 1`; time rescale must be finite and positive. Malformed
numbers are rejected instead of being interpreted as zero.

### Cascade density guard

The cascade guard is disabled by default, so normal generation and quality are
unchanged. When `--max-cascade-tokens N` is nonzero, TRELLIS checks the exact
quantized token count after its normal resolution backoff and before the
high-resolution shape flow.

`--dense-policy fail` refuses the run, `fallback-512` reuses the already computed
low-resolution SLAT and resolves every downstream model, atlas, and decimation
choice to the 512 pipeline, and `allow` records the overflow but continues.
The server accepts `max_cascade_tokens` and `dense_policy`; invalid values return
HTTP 400, while a valid `fail` policy that trips returns HTTP 422. Hardware-specific
limits should be configured by the deployment rather than baked into the library.
The former `--max-1024-tokens` / `max_1024_tokens` names remain accepted as
compatibility aliases for the private v0.5.4 patch.

The postprocess matches the reference pipeline op for op (see
`docs/spec/27-reference-postprocess.md` / `28-divergence-matrix.md`): the raw
dual-grid mesh is welded and hole-filled, **remeshed with narrow-band UDF dual
contouring** into a single clean manifold, quadric-simplified to the face budget,
coarse-clustered with the reference's bottom-up normal-cone merge, unwrapped with
stock xatlas per cluster and packed at auto resolution (UVs normalized to fill the
full atlas), shaded per texel by trilinear sampling of the voxel PBR volume (with
BVH closest-point snap onto the original surface), gutter-filled with a Telea
inpaint port, and exported as a GLB with smooth normals and **lossy-WebP textures**
(`EXT_texture_webp`; PNG fallback when built with `-DTRELLIS_WEBP=OFF`). Output
quality is at parity with the reference CUDA postprocess on identical inputs.

### GLB diagnostics

`tools/glb_topology.py` is a dependency-free checker for triangle GLBs emitted
by trellis.cpp:

```bash
python3 tools/glb_topology.py out/model.glb
python3 tools/glb_topology.py --weld-tolerance 1e-6 out/qem.glb out/meshopt.glb
```

It reports vertex and face counts, bounds, invalid indices, non-finite positions,
raw and seam-insensitive topology, connected components, embedded image MIME
types, and required extensions. Raw topology retains UV/attribute vertex splits;
`welded@...` groups positions into quantized cells of the requested width, so it
is not an exact Euclidean-distance weld.

This tool is specialized diagnostics, not a general glTF validator. Exit status
`1` means a file was unreadable, unsupported, malformed, or contained invalid
indices or non-finite positions; reported boundary, non-manifold, degenerate,
component, image, and extension counts are informational. Use the
[Khronos glTF Validator](https://github.com/KhronosGroup/glTF-Validator) for
full format conformance.

`TRELLIS_DBG_*` environment variables toggle developer debug logging only; no
behavior-driving environment variables remain — use the flags above.
`--c2s-diagnostics` enables request-scoped subdivision, memory, graph-allocation,
and chunk-size telemetry; the HTTP equivalent is `c2s_diagnostics=on`. It is
off by default and does not change allocation or generation behavior.

### trellis-server

`trellis-server` keeps the process resident (no Vulkan re-init per request) and exposes:

```
GET  /health     -> "ok"
POST /generate      multipart/form-data with an "image" file part; optional text
                    fields "seed", "resolution" (512/1024/1536), "bg_removal"
                    (threshold|birefnet). Returns model/gltf-binary.
```

Launch-time flags (including `--res`) set the per-request defaults; each request can
override them with its own fields. Sampler multipart fields use underscores and
explicit strength/interval names: `sparse_steps`,
`sparse_guidance_strength`, `sparse_guidance_rescale`,
`sparse_guidance_interval_start`, `sparse_guidance_interval_end`, and
`sparse_rescale_t`, with equivalent `shape_*` and `texture_*` fields.
Invalid sampler overrides return HTTP 400 before image staging or model work.
The density equivalents are `max_cascade_tokens` and `dense_policy`.

## Pipeline

```
text prompt
  │  stable-diffusion.cpp (Z-Image)              [external binary]
  ▼
RGB image
  │  BiRefNet / RMBG  (background removal)        [GGML]   → RGBA cutout
  ▼
DINOv3 ViT-L/16 feature extractor                [GGML]   → patch tokens [N,1024]
  │  (image conditioning, + null cond for CFG)
  ▼
① Sparse-Structure flow DiT (1.3B, dense 16³)    [GGML]
  │  → 8-ch 16³ latent → SS conv3d decoder → 64³ occupancy → active voxels
  ▼
② Shape-SLAT flow DiT (1.3B, sparse)             [GGML]   → 32-ch latent / active voxel
  │  → FlexiDualGrid shape decoder (sparse ConvNeXt) → dual grid → FlexiCubes mesh
  ▼
③ Texture-SLAT flow DiT (1.3B, sparse)           [GGML]   → 32-ch latent / active voxel
  │  → Sparse U-Net tex decoder (6-ch PBR per voxel)
  ▼
textured mesh
  │  weld → fill → narrow-band DC remesh → QEM edge-collapse decimate → cluster + xatlas →
  │  trilinear PBR bake (BVH snap) → Telea inpaint    [decimate: CPU / CUDA / HIP / Vulkan]
  ▼
UV-textured GLB (WebP PBR textures)
```

The decimation is a faithful port of the reference's CuMesh QEM edge-collapse simplifier
(Garland-Heckbert quadrics + a skinny-triangle shape metric + flip rejection + boundary
weighting), replacing an off-the-shelf simplifier that produced a fragmented, non-adaptive
mesh. The result matches the reference's adaptive triangulation — a single watertight
component with reference-level surface smoothness. It runs on the GPU (CUDA, ROCm/HIP, or a
Vulkan compute shader) behind one dispatch, with an automatic CPU fallback.

All three flow stages use a `FlowEulerGuidanceIntervalSampler` (rectified-flow Euler,
12 steps, classifier-free guidance with a guidance interval + rescale). Optional
512→1024 cascade for higher resolution.

The 1024 cascade runs on a 16 GB card thanks to **FlashAttention with padded K/V**
(`src/dit.cpp::sdpa`): the manual softmax needed a single ~18 GB score-matrix alloc at
the sparse-structure stage, and at the HR token count (≈53k) ggml's tiled FA NaN'd on
the unpadded last key-tile — zero-padding K/V to a 256 multiple + BF16 fixes both.
f16 compute is the default and matches torch (`--f32` forces f32; `--no-fa` restores
the plain-softmax path for A/B testing).

Every neural component is validated against PyTorch (the `trellis-test-*` binaries +
`tools/ref_*.py`): SS sampler matches torch to rel 4.3e-3 (exact voxel match), DiT
2.8e-3, DINOv3 1.8e-2, sparse conv 1e-3, BiRefNet 4e-4, C2S exact.

## Models

**Pre-built GGUFs:** [`ilintar/trellis2-gguf`](https://huggingface.co/ilintar/trellis2-gguf) —
download the full set and point `trellis-cli` / `trellis-server` (`--models DIR`) at that
folder. Or convert your own from the source checkpoints below (see `docs/spec/` and
`tools/` for the safetensors→GGUF conversion).

| role | source | notes |
|------|--------|-------|
| SS flow DiT | `microsoft/TRELLIS.2-4B` `ss_flow_img_dit_1_3B_64` | 1.3B, bf16 |
| Shape SLAT flow | `…/slat_flow_img2shape_dit_1_3B_{512,1024}` | 1.3B, bf16; `_1024` drives the cascade's HR pass |
| Tex SLAT flow | `…/slat_flow_imgshape2tex_dit_1_3B_{512,1024}` | 1.3B, bf16; `_1024` drives the cascade's HR pass |
| Shape decoder | `…/shape_dec_next_dc_f16c32` | FlexiDualGrid VAE |
| Tex decoder | `…/tex_dec_next_dc_f16c32` | Sparse U-Net VAE, 6-ch |
| SS decoder | `microsoft/TRELLIS-image-large` `ss_dec_conv3d_16l8` | reused from v1 |
| Image cond | `timm/vit_large_patch16_dinov3.lvd1689m` | ungated mirror of DINOv3 ViT-L (same weights) |
| BG removal | `ZhengPeng7/BiRefNet` | ungated BiRefNet (RMBG-2.0 substitute) |

The two helper models are HF-gated upstream; the ungated equivalents above avoid needing
a token.

## Performance

Measured end-to-end (image → GLB, res 1024, 12-step flows, includes model load;
`docs/spec/29-perf-profile.md` has the per-flow breakdown and methodology):

| input class | Strix Halo iGPU (Vulkan) | RTX 5060 Ti (CUDA) | reference Python, same GPU (cold) |
|---|---|---|---|
| light (goblin, 17k HR tokens) | 6:09 | **3:16** | 3:37 |
| heavy (turret, ~45k HR tokens) | 12:39 | **7:23** | 5:20 |

The postprocess (weld → remesh → decimate → unwrap → bake → WebP) uses the faithful
CuMesh QEM decimation described above. With a GPU backend it decimates a 5.4M→300k-face
mesh in ~5 s on ROCm (gfx1151) — ~18× the single-threaded CPU path — matching the
reference's on-GPU decimation; the CPU fallback stays available for GPU-less builds.
On Strix Halo, Vulkan is the fastest backend: ROCm requires
`GGML_CUDA_DISABLE_GRAPHS=1` (ggml's HIP graph capture stalls on these graphs)
and still trails Vulkan by 10–40 %.

## Tools

| tool | purpose |
|------|---------|
| `post-replay <dump.bin> <out.glb>` | re-run the whole postprocess from a `TRELLIS_DUMP_POST` dump in seconds (flags: `--no-remesh`, `--band`, `--no-snap`, `--box-uv`, `--faces`, `--meshopt --stop target|error|range`, `--atlas`, …) |
| `tools/glb_metrics.py` | CPU geometry/UV/material metrics (components, boundary edges, winding, texel density, doubleSided/WebP flags) for ours-vs-reference GLB comparison |
| `tools/render_glb.py` / `render_glb_fast.py` | quick multi-view flat renders |
| `tools/mv_preview/` | PBR-correct GLB previews via the `<model-viewer>` web component (see its README) |

`post-replay` uses the production QEM decimator by default. Its optional
`--meshopt` path keeps meshoptimizer's topology guards, keeps pruning disabled,
and disables the historical FQMS fallback. It has three explicit stop policies:

| policy | flags | meaning |
|--------|-------|---------|
| fixed target | `--meshopt --stop target --faces N` | existing guarded target behavior (also the default when only `--meshopt` is given) |
| error driven | `--meshopt --stop error --error-percent P` | use a zero triangle target and remove the cheapest legal collapses until the relative error budget or topology stops progress; an unlimited-error probe distinguishes those stop causes without replacing the quality result |
| bounded | `--meshopt --stop range --min-faces A --max-faces B --error-percent P` | protect `A` as a real floor, find the error-limited quality result toward it, and if necessary run an explicit count-first pass toward `B` while preserving the floor |

`P` is deformation relative to the mesh extent, not a percentage of triangles:
meshoptimizer defines the extent as the longest side of the input axis-aligned
bounding box. Thus `1` means a 1% relative deformation limit; the policy log
also prints the corresponding distance in mesh units. This is a geometric
quadric-error measure, not an angle threshold or a guaranteed Hausdorff bound.
Every adaptive run emits one machine-parseable `[meshopt-policy]` line with the
requested/result error, quality/output face counts, constraints, force status,
and stop reason. Because meshoptimizer collapses in batches, bounded mode retries
from the original mesh with a raised internal target when a batch would cross
the minimum; the original is the deterministic safe fallback. The helper
compacts exactly the returned triangles, and the adaptive post-replay path skips
the legacy second weld/fill/component-drop pass so its reported count remains
the reducer's final count. If no output fits the requested band, the log reports
`stop_reason=band-unreachable` while keeping the protected floor. Adaptive-only
flags are rejected outside their matching stop mode. `--meshopt` and `--decim`
are mutually exclusive.

## Licensing

Original trellis.cpp contributions are under the repository's MIT `LICENSE`.
Bundled and adapted code retains its own notices in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md); release archives must ship
both files. The native C++ post-processing/export path in this repository does
not link `nvdiffrast` or `nvdiffrec`.

CUDA release archives additionally contain NVIDIA's proprietary `cudart`,
`cublas`, and `cublasLt` redistributables. Their
`NVIDIA-CUDA-LICENSES` directory carries the pinned NVIDIA component terms;
the repository MIT license does not cover those binaries.

Model files are a separate license domain. TRELLIS.2 and BiRefNet weights are
MIT-licensed, while DINOv3 uses Meta's custom license with redistribution,
trade-control, and prohibited-use conditions. Distribute the exact model
licenses with any weight or GGUF bundle.

The QEM path is a declared CuMesh port and carries CuMesh's MIT notice. CuMesh
names PaMO (AGPL-3.0) as a reference implementation for its parallel
simplifier; this repository has not established that copyrightable PaMO code is
present. That reference alone does not establish AGPL coverage of ordinary
generated output; copyright coverage remains fact-specific if an output embeds
covered material. Closed-source software distributors should resolve the code
provenance question or use an independently licensed simplification path.

## Building

GGML is vendored in `thirdparty/ggml`. Pick a backend:

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGGML_VULKAN=ON   # Vulkan
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGGML_CUDA=ON    # CUDA
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGGML_HIP=ON     # ROCm
cmake --build build -j
```

See `.github/workflows/release.yml` for the exact flags the release binaries use
(GPU target lists, `-DGGML_OPENMP=OFF` on Windows).

## Layout

```
src/            C++ implementation (models, ops, pipeline, drivers)
src/test_*.cpp  parity / unit tests vs reference tensors (built as the trellis-test-* binaries)
include/        public headers
tools/          python conversion scripts (safetensors → GGUF) + reference-dump checks, via the uv venv
docs/spec/      reverse-engineered per-component architecture spec
thirdparty/     vendored ggml (gitignored), plus stb + xatlas
```
