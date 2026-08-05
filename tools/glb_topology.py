#!/usr/bin/env python3
"""Report topology and embedding checks for a trellis.cpp GLB.

This intentionally uses only Python's standard library. It understands the
single-scene, triangle-primitive layout emitted by trellis.cpp; it is not a
general-purpose glTF validator.

Raw topology retains attribute/UV-split vertices. The ``welded@...`` view groups
positions by quantized cells whose width is ``--weld-tolerance``; it is a useful
seam-insensitive diagnostic, not an exact Euclidean-distance weld.

Exit status 1 means a file could not be read or parsed, or contains invalid
indices or non-finite positions. Reported boundary, non-manifold, degenerate,
component, image, and extension counts are informational and do not change the
exit status. Command-line usage errors return 2.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
import math
from pathlib import Path
import struct
import sys


COMPONENT_FORMAT = {
    5120: "b",
    5121: "B",
    5122: "h",
    5123: "H",
    5124: "i",
    5125: "I",
    5126: "f",
}
TYPE_COMPONENTS = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
}


class UnionFind:
    def __init__(self, count: int) -> None:
        self.parent = list(range(count))

    def find(self, value: int) -> int:
        while self.parent[value] != value:
            self.parent[value] = self.parent[self.parent[value]]
            value = self.parent[value]
        return value

    def union(self, left: int, right: int) -> None:
        left = self.find(left)
        right = self.find(right)
        if left != right:
            self.parent[right] = left


def parse_glb(path: Path) -> tuple[dict, bytes]:
    data = path.read_bytes()
    if len(data) < 20 or data[:4] != b"glTF":
        raise ValueError("not a binary glTF file")
    magic, version, total_length = struct.unpack_from("<4sII", data)
    if magic != b"glTF" or version != 2 or total_length != len(data):
        raise ValueError("invalid GLB header")
    document = None
    binary = b""
    offset = 12
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError("truncated GLB chunk header")
        length, kind = struct.unpack_from("<II", data, offset)
        chunk_end = offset + 8 + length
        if chunk_end > len(data):
            raise ValueError("truncated GLB chunk payload")
        payload = data[offset + 8 : chunk_end]
        if kind == 0x4E4F534A:
            document = json.loads(payload)
        elif kind == 0x004E4942:
            binary = payload
        offset = chunk_end
    if offset != len(data):
        raise ValueError("invalid GLB chunk layout")
    if document is None:
        raise ValueError("GLB has no JSON chunk")
    if not isinstance(document, dict):
        raise ValueError("GLB JSON chunk must be an object")
    return document, binary


def nonnegative_int(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ValueError(f"{label} must be a non-negative integer")
    return value


def read_accessor(document: dict, binary: bytes, index: int) -> list[tuple]:
    accessors = document.get("accessors")
    views = document.get("bufferViews")
    if not isinstance(accessors, list) or not isinstance(views, list):
        raise ValueError("accessors and bufferViews must be arrays")
    if isinstance(index, bool) or not isinstance(index, int) or not 0 <= index < len(accessors):
        raise ValueError("accessor index is out of range")
    accessor = accessors[index]
    if not isinstance(accessor, dict):
        raise ValueError("accessor must be an object")

    component_format = COMPONENT_FORMAT[accessor["componentType"]]
    component_count = TYPE_COMPONENTS[accessor["type"]]
    view_index = nonnegative_int(accessor["bufferView"], "accessor bufferView")
    if view_index >= len(views):
        raise ValueError("bufferView index is out of range")
    view = views[view_index]
    if not isinstance(view, dict):
        raise ValueError("bufferView must be an object")
    if nonnegative_int(view.get("buffer", 0), "buffer index") != 0:
        raise ValueError("only GLB buffer 0 is supported")

    item_format = "<" + component_format * component_count
    item_size = struct.calcsize(item_format)
    count = nonnegative_int(accessor["count"], "accessor count")
    view_offset = nonnegative_int(view.get("byteOffset", 0), "bufferView byteOffset")
    view_length = nonnegative_int(view["byteLength"], "bufferView byteLength")
    accessor_offset = nonnegative_int(
        accessor.get("byteOffset", 0), "accessor byteOffset"
    )
    stride = nonnegative_int(view.get("byteStride", item_size), "bufferView byteStride")
    if stride < item_size:
        raise ValueError("bufferView byteStride is smaller than the accessor item")
    if view_offset + view_length > len(binary):
        raise ValueError("bufferView exceeds the GLB binary chunk")

    item_span = 0 if count == 0 else (count - 1) * stride + item_size
    if accessor_offset + item_span > view_length:
        raise ValueError("accessor exceeds its bufferView")

    offset = view_offset + accessor_offset
    return [
        struct.unpack_from(item_format, binary, offset + item * stride)
        for item in range(count)
    ]


def geometry(document: dict, binary: bytes) -> tuple[list[tuple], list[tuple[int, int, int]]]:
    meshes = document.get("meshes", [])
    if not isinstance(meshes, list):
        raise ValueError("meshes must be an array")

    positions: list[tuple] = []
    faces: list[tuple[int, int, int]] = []
    for mesh in meshes:
        if not isinstance(mesh, dict):
            raise ValueError("mesh must be an object")
        primitives = mesh.get("primitives", [])
        if not isinstance(primitives, list):
            raise ValueError("mesh primitives must be an array")
        for primitive in primitives:
            if not isinstance(primitive, dict):
                raise ValueError("mesh primitive must be an object")
            if primitive.get("mode", 4) != 4:
                raise ValueError("non-triangle primitive is unsupported")
            attributes = primitive.get("attributes")
            if not isinstance(attributes, dict) or "POSITION" not in attributes:
                raise ValueError("triangle primitive has no POSITION accessor")

            base = len(positions)
            primitive_positions = read_accessor(
                document, binary, attributes["POSITION"]
            )
            if "indices" in primitive:
                indices = [
                    value[0]
                    for value in read_accessor(document, binary, primitive["indices"])
                ]
            else:
                indices = list(range(len(primitive_positions)))
            if len(indices) % 3:
                raise ValueError("triangle index count is not divisible by three")

            def global_index(index_value: int) -> int:
                if 0 <= index_value < len(primitive_positions):
                    return index_value + base
                return -1

            positions.extend(primitive_positions)
            faces.extend(
                tuple(global_index(indices[item + corner]) for corner in range(3))
                for item in range(0, len(indices), 3)
            )
    if not positions or not faces:
        raise ValueError("GLB contains no triangle mesh")
    return positions, faces


def weld_positions(positions: list[tuple], tolerance: float) -> tuple[list[int], int]:
    """Group positions by rounded coordinate cells.

    ``tolerance`` is the cell width on each axis, not a Euclidean-distance
    threshold. This keeps seam-insensitive reports deterministic and cheap.
    """
    mapping: dict[tuple[int, int, int], int] = {}
    remap: list[int] = []
    for x, y, z in positions:
        key = (round(x / tolerance), round(y / tolerance), round(z / tolerance))
        remap.append(mapping.setdefault(key, len(mapping)))
    return remap, len(mapping)


def topology(faces: list[tuple[int, int, int]], vertex_count: int) -> dict:
    edges: Counter[tuple[int, int]] = Counter()
    union = UnionFind(vertex_count)
    valid_faces: list[tuple[int, int, int]] = []
    degenerate = 0
    for a, b, c in faces:
        if a == b or b == c or c == a:
            degenerate += 1
            continue
        valid_faces.append((a, b, c))
        union.union(a, b)
        union.union(b, c)
        edges.update((tuple(sorted((a, b))), tuple(sorted((b, c))), tuple(sorted((c, a)))))
    component_faces: Counter[int] = Counter(union.find(face[0]) for face in valid_faces)
    boundary = sum(count == 1 for count in edges.values())
    nonmanifold = sum(count > 2 for count in edges.values())
    return {
        "boundary": boundary,
        "nonmanifold": nonmanifold,
        "degenerate": degenerate,
        "components": len(component_faces),
        "largest_component_faces": max(component_faces.values(), default=0),
        "watertight": boundary == 0 and nonmanifold == 0 and degenerate == 0,
    }


def embedded_images(document: dict) -> list[str]:
    result = []
    for image in document.get("images", []):
        if "bufferView" in image:
            result.append(image.get("mimeType", "unknown"))
    return result


def report(path: Path, tolerance: float) -> int:
    document, binary = parse_glb(path)
    positions, faces = geometry(document, binary)
    invalid_indices = sum(
        index < 0 or index >= len(positions) for face in faces for index in face
    )
    nonfinite_positions = sum(
        not math.isfinite(component) for position in positions for component in position
    )
    bounds_min = tuple(min(position[axis] for position in positions) for axis in range(3))
    bounds_max = tuple(max(position[axis] for position in positions) for axis in range(3))
    extents = tuple(bounds_max[axis] - bounds_min[axis] for axis in range(3))
    raw = topology(faces, len(positions)) if not invalid_indices else None
    welded_count = 0
    welded = None
    if not invalid_indices and not nonfinite_positions:
        remap, welded_count = weld_positions(positions, tolerance)
        welded_faces = [tuple(remap[index] for index in face) for face in faces]
        welded = topology(welded_faces, welded_count)

    print(path)
    print(f"  vertices={len(positions)} faces={len(faces)}")
    print(
        "  bounds_extent="
        + ",".join(f"{extent:.6g}" for extent in extents)
        + " bounds_min="
        + ",".join(f"{value:.6g}" for value in bounds_min)
        + " bounds_max="
        + ",".join(f"{value:.6g}" for value in bounds_max)
    )
    print(f"  invalid_indices={invalid_indices} nonfinite_position_values={nonfinite_positions}")
    if raw:
        print(
            "  raw: "
            f"boundary={raw['boundary']} nonmanifold={raw['nonmanifold']} "
            f"degenerate={raw['degenerate']} components={raw['components']}"
        )
    if welded:
        print(
            f"  welded@{tolerance:g}: vertices={welded_count} "
            f"boundary={welded['boundary']} nonmanifold={welded['nonmanifold']} "
            f"degenerate={welded['degenerate']} components={welded['components']} "
            f"largest_component_faces={welded['largest_component_faces']} "
            f"watertight={'yes' if welded['watertight'] else 'no'}"
        )
    images = embedded_images(document)
    print(f"  embedded_images={len(images)} mime_types={','.join(images) if images else 'none'}")
    print(f"  extensions_required={','.join(document.get('extensionsRequired', [])) or 'none'}")
    return 1 if invalid_indices or nonfinite_positions else 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Report topology and embedded-payload checks for trellis.cpp GLBs."
    )
    parser.add_argument("glb", type=Path, nargs="+")
    parser.add_argument(
        "--weld-tolerance",
        type=float,
        default=1e-6,
        help="quantized positional-weld cell width (default: 1e-6)",
    )
    args = parser.parse_args()
    if not math.isfinite(args.weld_tolerance) or args.weld_tolerance <= 0:
        parser.error("--weld-tolerance must be finite and positive")
    status = 0
    for path in args.glb:
        try:
            status |= report(path, args.weld_tolerance)
        except (
            AttributeError,
            IndexError,
            KeyError,
            OSError,
            OverflowError,
            struct.error,
            TypeError,
            ValueError,
        ) as error:
            print(f"{path}: error: {error}", file=sys.stderr)
            status = 1
    return status


if __name__ == "__main__":
    raise SystemExit(main())
