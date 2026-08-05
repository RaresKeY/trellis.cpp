# NVIDIA CUDA runtime redistributables

CUDA release archives of trellis.cpp may include the following proprietary
NVIDIA CUDA Toolkit runtime components:

- CUDA Runtime (`cudart`)
- CUDA Basic Linear Algebra Subprograms library (`cublas`)
- CUDA Basic Linear Algebra Subprograms light library (`cublasLt`)

These components are copyright NVIDIA Corporation. They are not covered by the
trellis.cpp MIT license. They are distributed under the NVIDIA Software License
Agreement and CUDA Toolkit Supplement:

https://docs.nvidia.com/cuda/archive/13.1.1/eula/index.html

Use of the CUDA build and these components is subject to that agreement. In
particular, the components are supplied only for access by trellis.cpp on
systems with NVIDIA GPUs, not as a stand-alone SDK. They may not be modified or
redistributed except as the NVIDIA agreement permits.
No NVIDIA sponsorship or endorsement is claimed.

The `NVIDIA-CUDA-LICENSES` directory in each CUDA release archive includes the
exact NVIDIA component license files for the pinned CUDA Toolkit release. Those
files control if this summary conflicts with them.
