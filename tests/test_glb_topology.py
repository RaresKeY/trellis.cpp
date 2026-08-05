#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "glb_topology.py"
JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942

TETRA_POSITIONS = [
    (0.0, 0.0, 0.0),
    (1.0, 0.0, 0.0),
    (0.0, 1.0, 0.0),
    (0.0, 0.0, 1.0),
]
TETRA_FACES = [
    (0, 2, 1),
    (0, 1, 3),
    (1, 2, 3),
    (2, 0, 3),
]


def pad4(payload: bytes, fill: bytes) -> bytes:
    return payload + fill * ((-len(payload)) % 4)


def write_document_glb(path: Path, document: object, binary: bytes = b"") -> Path:
    json_payload = pad4(
        json.dumps(document, separators=(",", ":")).encode("utf-8"), b" "
    )
    binary_payload = pad4(binary, b"\0")
    chunks = (
        struct.pack("<II", len(json_payload), JSON_CHUNK)
        + json_payload
        + struct.pack("<II", len(binary_payload), BIN_CHUNK)
        + binary_payload
    )
    path.write_bytes(struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks)
    return path


def write_glb(
    path: Path,
    positions: list[tuple[float, float, float]],
    faces: list[tuple[int, int, int]],
    *,
    image_mime_types: tuple[str, ...] = ("image/png", "image/png"),
    extensions_required: tuple[str, ...] = (),
) -> Path:
    indices = [index for face in faces for index in face]
    position_bytes = b"".join(struct.pack("<3f", *position) for position in positions)
    index_bytes = struct.pack(f"<{len(indices)}H", *indices)

    binary_parts: list[bytes] = []
    buffer_views: list[dict] = []

    def add_view(payload: bytes) -> int:
        offset = sum(len(part) for part in binary_parts)
        buffer_views.append(
            {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
        )
        binary_parts.append(payload)
        return len(buffer_views) - 1

    position_view = add_view(position_bytes)
    index_view = add_view(index_bytes)
    images = []
    for mime_type in image_mime_types:
        image_view = add_view(b"\x89PNG\r\n\x1a\n")
        images.append({"bufferView": image_view, "mimeType": mime_type})

    binary = b"".join(binary_parts)
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": buffer_views,
        "accessors": [
            {
                "bufferView": position_view,
                "componentType": 5126,
                "count": len(positions),
                "type": "VEC3",
            },
            {
                "bufferView": index_view,
                "componentType": 5123,
                "count": len(indices),
                "type": "SCALAR",
            },
        ],
        "meshes": [
            {
                "primitives": [
                    {
                        "attributes": {"POSITION": 0},
                        "indices": 1,
                        "mode": 4,
                    }
                ]
            }
        ],
        "nodes": [{"mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
        "images": images,
    }
    if extensions_required:
        document["extensionsRequired"] = list(extensions_required)

    return write_document_glb(path, document, binary)


def seam_split_tetrahedron() -> tuple[
    list[tuple[float, float, float]], list[tuple[int, int, int]]
]:
    positions = []
    faces = []
    for face in TETRA_FACES:
        base = len(positions)
        positions.extend(TETRA_POSITIONS[index] for index in face)
        faces.append((base, base + 1, base + 2))
    return positions, faces


def accessor_document() -> tuple[dict, bytes]:
    position_bytes = b"".join(
        struct.pack("<3f", *position) for position in TETRA_POSITIONS[:3]
    )
    index_bytes = struct.pack("<3H", 0, 1, 2)
    binary = position_bytes + index_bytes
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(position_bytes)},
            {
                "buffer": 0,
                "byteOffset": len(position_bytes),
                "byteLength": len(index_bytes),
            },
        ],
        "accessors": [
            {
                "bufferView": 0,
                "componentType": 5126,
                "count": 3,
                "type": "VEC3",
            },
            {
                "bufferView": 1,
                "componentType": 5123,
                "count": 3,
                "type": "SCALAR",
            },
        ],
        "meshes": [
            {
                "primitives": [
                    {"attributes": {"POSITION": 0}, "indices": 1, "mode": 4}
                ]
            }
        ],
    }
    return document, binary


class GlbTopologyTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary_directory.name)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def run_tool(self, *arguments: object) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(TOOL), *(str(argument) for argument in arguments)],
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
        )

    def test_valid_tetrahedron_reports_topology_and_payload(self) -> None:
        path = write_glb(
            self.directory / "valid.glb",
            TETRA_POSITIONS,
            TETRA_FACES,
            extensions_required=("TEST_required",),
        )

        result = self.run_tool(path)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertIn("vertices=4 faces=4", result.stdout)
        self.assertIn(
            "raw: boundary=0 nonmanifold=0 degenerate=0 components=1",
            result.stdout,
        )
        self.assertIn(
            "welded@1e-06: vertices=4 boundary=0 nonmanifold=0 "
            "degenerate=0 components=1 largest_component_faces=4 watertight=yes",
            result.stdout,
        )
        self.assertIn(
            "embedded_images=2 mime_types=image/png,image/png", result.stdout
        )
        self.assertIn("extensions_required=TEST_required", result.stdout)

    def test_quantized_weld_removes_uv_seam_splits(self) -> None:
        positions, faces = seam_split_tetrahedron()
        path = write_glb(self.directory / "seams.glb", positions, faces)

        result = self.run_tool(path)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("vertices=12 faces=4", result.stdout)
        self.assertIn(
            "raw: boundary=12 nonmanifold=0 degenerate=0 components=4",
            result.stdout,
        )
        self.assertIn(
            "welded@1e-06: vertices=4 boundary=0 nonmanifold=0 "
            "degenerate=0 components=1 largest_component_faces=4 watertight=yes",
            result.stdout,
        )

    def test_truncated_chunk_is_a_clean_input_error(self) -> None:
        path = self.directory / "truncated.glb"
        path.write_bytes(
            struct.pack("<4sII", b"glTF", 2, 24)
            + struct.pack("<II", 8, JSON_CHUNK)
            + b"{}  "
        )

        result = self.run_tool(path)

        self.assertEqual(result.returncode, 1)
        self.assertIn("truncated GLB chunk payload", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_out_of_range_index_returns_one_without_a_traceback(self) -> None:
        path = write_glb(
            self.directory / "bad-index.glb",
            TETRA_POSITIONS[:3],
            [(0, 1, 9)],
        )

        result = self.run_tool(path)

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, "")
        self.assertIn("invalid_indices=1 nonfinite_position_values=0", result.stdout)
        self.assertNotIn("raw:", result.stdout)
        self.assertNotIn("welded@", result.stdout)
        self.assertNotIn("Traceback", result.stdout + result.stderr)

    def test_nonfinite_position_returns_one_and_skips_weld(self) -> None:
        path = write_glb(
            self.directory / "nonfinite.glb",
            [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (float("nan"), 1.0, 0.0)],
            [(0, 1, 2)],
        )

        result = self.run_tool(path)

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, "")
        self.assertIn("invalid_indices=0 nonfinite_position_values=1", result.stdout)
        self.assertIn(
            "raw: boundary=3 nonmanifold=0 degenerate=0 components=1",
            result.stdout,
        )
        self.assertNotIn("welded@", result.stdout)
        self.assertNotIn("Traceback", result.stdout + result.stderr)

    def test_multiple_files_or_their_exit_status(self) -> None:
        valid = write_glb(
            self.directory / "valid-multiple.glb", TETRA_POSITIONS, TETRA_FACES
        )
        invalid = write_glb(
            self.directory / "invalid-multiple.glb",
            TETRA_POSITIONS[:3],
            [(0, 1, 9)],
        )

        result = self.run_tool(valid, invalid)

        self.assertEqual(result.returncode, 1)
        self.assertIn(str(valid), result.stdout)
        self.assertIn(str(invalid), result.stdout)
        self.assertNotIn("Traceback", result.stdout + result.stderr)

    def test_non_object_json_is_a_clean_input_error(self) -> None:
        path = write_document_glb(self.directory / "json-array.glb", [])

        result = self.run_tool(path)

        self.assertEqual(result.returncode, 1)
        self.assertIn("GLB JSON chunk must be an object", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_malformed_accessor_spans_are_rejected_before_unpacking(self) -> None:
        cases = (
            ("zero-stride", "view", "byteStride", 0, "byteStride is smaller"),
            (
                "huge-count",
                "accessor",
                "count",
                1_000_000_000,
                "accessor exceeds its bufferView",
            ),
            (
                "negative-offset",
                "view",
                "byteOffset",
                -1,
                "byteOffset must be a non-negative integer",
            ),
        )
        for name, target, key, value, expected in cases:
            with self.subTest(name=name):
                document, binary = accessor_document()
                if target == "view":
                    document["bufferViews"][0][key] = value
                else:
                    document["accessors"][0][key] = value
                path = write_document_glb(
                    self.directory / f"{name}.glb", document, binary
                )

                result = self.run_tool(path)

                self.assertEqual(result.returncode, 1)
                self.assertIn(expected, result.stderr)
                self.assertNotIn("Traceback", result.stderr)

    def test_invalid_weld_tolerance_is_a_usage_error(self) -> None:
        path = write_glb(
            self.directory / "tolerance.glb", TETRA_POSITIONS, TETRA_FACES
        )
        for value in ("0", "-1", "nan", "inf"):
            with self.subTest(value=value):
                result = self.run_tool("--weld-tolerance", value, path)
                self.assertEqual(result.returncode, 2)
                self.assertIn(
                    "--weld-tolerance must be finite and positive", result.stderr
                )
                self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
