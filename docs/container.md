# CUDA container image

The repository builds its CUDA backend directly from the checked-out source and
publishes it to GitHub Container Registry:

```text
ghcr.io/rareskey/trellis.cpp
```

The published image is Linux/amd64 and is compiled for CUDA SM75/Turing GPUs,
including the GeForce RTX 2080 Ti. It includes `trellis-server`, `trellis-cli`,
and `post-replay`, and contains no model weights. Mount a compatible GGUF model
directory at `/models`. It is compiled from the exact public-repository checkout
and recursive submodules; it does not use the private `trellis2` patch stack.

## Tags

Every successful build of `main` publishes `main` and `sha-<commit>` tags. A Git tag such as
`v1.2.3` additionally publishes:

```text
v1.2.3
v1.2
v1
latest
```

Prereleases such as `v1.2.3-rc.1` receive only their exact prerelease tag and do
not move `latest`. Pull requests build the complete image without publishing it.
The workflow can also be run manually; publication is limited to `main` and
`v*` refs.

The first versioned package is created only after a valid version tag is pushed;
the repository does not synthesize a release tag.

GitHub creates a new Container registry package under a personal account as
private, even when its source repository is public. After the first successful
`main` publication, open the package settings and use **Change visibility** to
make it public if anonymous pulls are intended. Until then, authenticate before
pulling with `docker login ghcr.io` or `podman login ghcr.io`.

For reproducible deployments, use the `sha-<commit>` tag or the immutable digest
shown by GitHub Packages instead of a moving major, minor, `main`, or `latest`
tag.

## Run the server

Docker with NVIDIA Container Toolkit:

```bash
docker run --rm --gpus all \
  --publish 127.0.0.1:8080:8080 \
  --volume /absolute/path/to/models:/models:ro \
  ghcr.io/rareskey/trellis.cpp:v1.2.3
```

Rootless Podman with NVIDIA CDI:

```bash
podman run --rm --device nvidia.com/gpu=all \
  --publish 127.0.0.1:8080:8080 \
  --volume /absolute/path/to/models:/models:ro \
  ghcr.io/rareskey/trellis.cpp:v1.2.3
```

The image defaults to
`trellis-server --models /models --gpu 0 --host 0.0.0.0 --port 8080 --require-gpu`.
Arguments after the image name replace that entire default command. To customize
server flags, repeat the executable and every option you want to retain. For
example, to select GPU 1:

```bash
docker run --rm --gpus all \
  --publish 127.0.0.1:8080:8080 \
  --volume /absolute/path/to/models:/models:ro \
  ghcr.io/rareskey/trellis.cpp:v1.2.3 \
  trellis-server \
  --models /models \
  --gpu 1 \
  --host 0.0.0.0 \
  --port 8080 \
  --require-gpu
```

The loopback-only publication above is deliberate because the server does not
provide authentication; add an authenticated reverse proxy before exposing it
to another network.

Run the CLI or replay tool by replacing the image command. Disable the
server-specific health check for long-running one-shot tools:

```bash
docker run --rm --gpus all --no-healthcheck \
  --volume /absolute/path/to/models:/models:ro \
  --volume "$PWD:/work" \
  ghcr.io/rareskey/trellis.cpp:v1.2.3 \
  trellis-cli \
  /work/input.png /work/output.glb --models /models --require-gpu
```

## Build locally

The CI-published image targets CUDA architecture 75 (Turing/SM75). The
Containerfile keeps the architecture build argument overrideable for local
images. For example, build for Ampere/SM86 with:

```bash
podman build \
  --build-arg 'CMAKE_CUDA_ARCHITECTURES=86' \
  --tag localhost/trellis.cpp:sm86 \
  --file Containerfile .
```

Multiple architectures can still be requested locally with a semicolon-separated
value such as `75;86;89`, but each extra architecture materially increases CUDA
template compilation time and image build cost.

The runtime stage is based on NVIDIA's CUDA runtime image and retains its
container license. The project `LICENSE` and `THIRD_PARTY_NOTICES.md` are copied
to `/usr/share/licenses/trellis.cpp/`.
