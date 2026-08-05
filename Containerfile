# syntax=docker/dockerfile:1.7

# Keep these bases aligned with the CUDA version used by the native release job.
# The image is intentionally linux/amd64-only; the CUDA architecture list can be
# overridden at build time when a smaller, GPU-specific image is preferred.
FROM docker.io/nvidia/cuda:13.1.2-devel-ubuntu22.04@sha256:7d1c645f99a4829ae31ceb88cce3bcf330ce45486e48abc68400764d581c07cd AS builder

ARG CMAKE_CUDA_ARCHITECTURES="75;80;86;89;90;120"
ARG BUILD_JOBS=2

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install --yes --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src/trellis
COPY . .

# A source archive without the recursive ggml submodule cannot produce a
# working CUDA build. Fail here with a useful message instead of much later in
# CMake configuration.
RUN test -s thirdparty/ggml/CMakeLists.txt

# Link against the CUDA driver stub while building. The real driver is injected
# by NVIDIA Container Toolkit/CDI on the host at runtime.
RUN test -e /usr/local/cuda/lib64/stubs/libcuda.so.1 \
    || ln -s libcuda.so /usr/local/cuda/lib64/stubs/libcuda.so.1

RUN cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CUDA_ARCHITECTURES="${CMAKE_CUDA_ARCHITECTURES}" \
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath-link,/usr/local/cuda/lib64/stubs" \
        -DGGML_CUDA=ON \
        -DGGML_NATIVE=OFF \
        -DBUILD_TESTING=OFF \
    && cmake --build /build \
        --target trellis-server trellis-cli post-replay \
        --parallel "${BUILD_JOBS}" \
    && mkdir -p /dist \
    && cp /build/trellis-server /build/trellis-cli /build/post-replay /dist/ \
    && cp -P /build/libggml*.so* /dist/ \
    && find /dist -type f -not -type l -exec strip --strip-unneeded '{}' +

FROM docker.io/nvidia/cuda:13.1.2-runtime-ubuntu22.04@sha256:2150d95359d4c9b22d36e00c0006c1c4fa1e51870b68de74645d20832e3a2fbc

LABEL org.opencontainers.image.title="trellis.cpp CUDA" \
      org.opencontainers.image.description="CUDA image-to-3D server, CLI, and post-processing tools built from trellis.cpp" \
      org.opencontainers.image.source="https://github.com/RaresKeY/trellis.cpp" \
      org.opencontainers.image.licenses="MIT"

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install --yes --no-install-recommends \
        ca-certificates \
        curl \
        libgomp1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /dist/ /opt/trellis/
COPY LICENSE THIRD_PARTY_NOTICES.md /usr/share/licenses/trellis.cpp/
COPY thirdparty/nvidia-cuda-runtime/README.md \
    /usr/share/licenses/trellis.cpp/NVIDIA-CUDA-RUNTIME-README.md

ENV PATH="/opt/trellis:${PATH}" \
    LD_LIBRARY_PATH="/opt/trellis:/usr/local/nvidia/lib:/usr/local/nvidia/lib64:/usr/local/cuda/lib64"

WORKDIR /work
EXPOSE 8080

HEALTHCHECK --interval=10s --timeout=3s --start-period=30s --retries=6 \
    CMD curl --fail --silent http://127.0.0.1:8080/health >/dev/null || exit 1

# NVIDIA's base-image entrypoint remains intact; replacing CMD selects another
# bundled executable without bypassing NVIDIA's runtime initialization.
CMD ["/opt/trellis/trellis-server", "--models", "/models", "--gpu", "0", "--host", "0.0.0.0", "--port", "8080", "--require-gpu"]
