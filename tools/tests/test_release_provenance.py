"""Offline release-provenance regression; uses synthetic build artifacts only."""
import importlib.util
import hashlib
import json
import struct
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "release_provenance.py"
spec = importlib.util.spec_from_file_location("release_provenance", SCRIPT)
provenance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provenance)
SHA = "a" * 40


class ReleaseProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "firmware/build"
        self.dist = self.root / "web/dist"
        (self.build / "bootloader").mkdir(parents=True)
        (self.build / "partition_table").mkdir()
        (self.dist / "assets").mkdir(parents=True)
        (self.build / "bootloader/bootloader.bin").write_bytes(b"BOOT")
        self.write_partition_binary()
        (self.build / "temperature_monitor.bin").write_bytes(b"APP-IMAGE")
        (self.build / "ota_data_initial.bin").write_bytes(b"OTA")
        (self.dist / "assets/main.js").write_bytes(b"console.log('ok')")
        (self.dist / "index.html").write_bytes(b"<html></html>")
        (self.root / "firmware/partitions.csv").write_text(
            "# Name, Type, SubType, Offset, Size, Flags\n"
            "nvs,data,nvs,0x9000,0x6000,\n"
            "otadata,data,ota,0xf000,0x2000,\n"
            "phy_init,data,phy,0x11000,0x1000,\n"
            "ota_0,app,ota_0,0x20000,0x400000,\n"
            "ota_1,app,ota_1,0x420000,0x400000,\n"
            "history,data,spiffs,0x820000,0x400000,\n", encoding="utf-8"
        )
        (self.root / "firmware/CMakeLists.txt").write_text(
            "project(temperature_monitor VERSION 0.1.0)\n", encoding="utf-8"
        )
        (self.root / "web/package.json").write_text(
            '{"version":"0.1.0"}', encoding="utf-8"
        )
        (self.root / "web/package-lock.json").write_text(
            '{"version":"0.1.0"}', encoding="utf-8"
        )
        self.flash_files = {
            "0x0": "bootloader/bootloader.bin",
            "0x8000": "partition_table/partition-table.bin",
            "0xf000": "ota_data_initial.bin",
            "0x20000": "temperature_monitor.bin",
        }
        self.save_flash_args()

    def save_flash_args(self):
        (self.build / "flasher_args.json").write_text(
            json.dumps({"flash_files": self.flash_files}), encoding="utf-8"
        )

    def test_manifest_and_checksums_are_deterministic_and_not_release_approved(self):
        output = self.build / "provenance"
        first = provenance.write_provenance(self.root, SHA, output)
        json_bytes = (output / "manifest.json").read_bytes()
        sums = (output / "SHA256SUMS.txt").read_bytes()
        second = provenance.write_provenance(self.root, SHA, output)
        self.assertEqual(first, second)
        self.assertEqual(json_bytes, (output / "manifest.json").read_bytes())
        self.assertEqual(sums, (output / "SHA256SUMS.txt").read_bytes())
        self.assertEqual(first["qualification"], "CI_BUILD_ONLY_NOT_RELEASE_APPROVED")
        self.assertEqual(first["source_commit"], SHA)
        self.assertEqual(first["ota_headroom_bytes"], 0x400000 - len(b"APP-IMAGE"))
        self.assertEqual(len(first["flash_images"]), 4)
        self.assertEqual(len(first["web_assets"]), 2)
        for line in sums.decode().splitlines():
            sha, name = line.split("  ", 1)
            self.assertEqual(sha, provenance.digest(self.root / name))

    def test_rejects_missing_and_wrong_flash_mapping(self):
        self.flash_files.pop("0x8000")
        self.save_flash_args()
        with self.assertRaisesRegex(ValueError, "required"):
            provenance.collect(self.root, SHA)
        self.flash_files["0x8000"] = "partition_table/partition-table.bin"
        self.flash_files["0x21000"] = self.flash_files.pop("0x20000")
        self.save_flash_args()
        with self.assertRaisesRegex(ValueError, "offset"):
            provenance.collect(self.root, SHA)

    def test_rejects_traversal_and_symlinks(self):
        self.flash_files["0x8000"] = "../web/package.json"
        self.save_flash_args()
        with self.assertRaisesRegex(ValueError, "unsafe"):
            provenance.collect(self.root, SHA)
        self.flash_files["0x8000"] = "partition_table/partition-table.bin"
        self.save_flash_args()
        (self.dist / "assets/escape").symlink_to(self.root / "web/package.json")
        with self.assertRaisesRegex(ValueError, "symlink"):
            provenance.collect(self.root, SHA)

    def test_rejects_oversize_image_and_overlapping_slots(self):
        (self.build / "temperature_monitor.bin").write_bytes(b"A" * (0x400000 + 1))
        with self.assertRaisesRegex(ValueError, "exceeds"):
            provenance.collect(self.root, SHA)
        (self.build / "temperature_monitor.bin").write_bytes(b"A")
        partitions = self.root / "firmware/partitions.csv"
        partitions.write_text(
            partitions.read_text(encoding="utf-8").replace(
                "ota_1,app,ota_1,0x420000", "ota_1,app,ota_1,0x30000"
            ), encoding="utf-8"
        )
        with self.assertRaisesRegex(ValueError, "overlapping"):
            provenance.collect(self.root, SHA)

    def test_rejects_version_drift_and_bad_commit(self):
        (self.root / "web/package.json").write_text(
            '{"version":"0.2.0"}', encoding="utf-8"
        )
        with self.assertRaisesRegex(ValueError, "versions disagree"):
            provenance.collect(self.root, SHA)
        with self.assertRaisesRegex(ValueError, "exact lowercase"):
            provenance.collect(self.root, "master")

    def test_rejects_missing_web_assets_and_duplicate_flash_targets(self):
        for item in self.dist.rglob("*"):
            if item.is_file():
                item.unlink()
        with self.assertRaisesRegex(ValueError, "empty"):
            provenance.collect(self.root, SHA)
        (self.dist / "index.html").write_bytes(b"web")
        self.flash_files["0x1000"] = "temperature_monitor.bin"
        self.save_flash_args()
        with self.assertRaisesRegex(ValueError, "duplicate"):
            provenance.collect(self.root, SHA)

    def test_rejects_corrupt_full_partition_layout(self):
        partitions = self.root / "firmware/partitions.csv"
        original = partitions.read_text(encoding="utf-8")
        cases = (
            ("history,data,spiffs,0x820000,0x400000",
             "history,data,spiffs,0xE00000,0x400000", "outside flash"),
            ("history,data,spiffs,0x820000,0x400000",
             "history,data,spiffs,0x420000,0x400000", "overlapping"),
            ("nvs,data,nvs,0x9000", "nvs,data,nvs,0x9001", "misaligned"),
            ("history,data,spiffs,0x820000,0x400000,\n", "",
             "missing or invalid"),
            ("history,data,spiffs,0x820000,0x400000,\n",
             "history,data,spiffs,0x820000,0x400000,\n"
             "history,data,spiffs,0x820000,0x400000,\n", "duplicate"),
        )
        for old, replacement, error in cases:
            with self.subTest(error=error, replacement=replacement):
                self.assertIn(old, original)
                partitions.write_text(original.replace(old, replacement),
                                      encoding="utf-8")
                with self.assertRaisesRegex(ValueError, error):
                    provenance.collect(self.root, SHA)
        partitions.write_text(original, encoding="utf-8")

    def test_rejects_misplaced_or_oversize_ota_metadata(self):
        self.flash_files["0xf100"] = self.flash_files.pop("0xf000")
        self.save_flash_args()
        with self.assertRaisesRegex(ValueError, "offset"):
            provenance.collect(self.root, SHA)
        self.flash_files["0xf000"] = self.flash_files.pop("0xf100")
        self.save_flash_args()
        (self.build / "ota_data_initial.bin").write_bytes(b"X" * 0x2001)
        with self.assertRaisesRegex(ValueError, "OTA metadata"):
            provenance.collect(self.root, SHA)

    def test_rejects_flash_images_in_data_partitions_or_beyond_flash(self):
        extra = self.build / "extra.bin"
        extra.write_bytes(b"X" * 4096)
        self.flash_files["0x9000"] = "extra.bin"
        self.save_flash_args()
        with self.assertRaisesRegex(ValueError, "overlaps partition"):
            provenance.collect(self.root, SHA)
        self.flash_files.pop("0x9000")
        self.flash_files["0x1000000"] = "extra.bin"
        self.save_flash_args()
        with self.assertRaisesRegex(ValueError, "physical flash"):
            provenance.collect(self.root, SHA)


    def write_partition_binary(self, entries=None):
        # Independent ESP-IDF binary fixture: magic, type/subtype, offset,
        # size, 16-byte name, flags; followed by the MD5 marker and padding.
        if entries is None:
            entries = [
                ("nvs", 1, 2, 0x9000, 0x6000),
                ("otadata", 1, 0, 0xF000, 0x2000),
                ("phy_init", 1, 1, 0x11000, 0x1000),
                ("ota_0", 0, 0x10, 0x20000, 0x400000),
                ("ota_1", 0, 0x11, 0x420000, 0x400000),
                ("history", 1, 0x82, 0x820000, 0x400000),
            ]
        binary = b"".join(
            struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size,
                        name.encode("ascii").ljust(16, b"\\x00"), 0)
            for name, kind, subtype, offset, size in entries
        )
        binary += b"\\xeb\\xeb" + b"\\xff" * 14 + hashlib.md5(binary).digest()
        binary += b"\\xff" * (0xC00 - len(binary))
        (self.build / "partition_table/partition-table.bin").write_bytes(binary)

    def test_rejects_stale_binary_partition_map_with_valid_md5(self):
        entries = [
            ("nvs", 1, 2, 0x9000, 0x6000),
            ("otadata", 1, 0, 0xF000, 0x2000),
            ("phy_init", 1, 1, 0x11000, 0x1000),
            ("ota_0", 0, 0x10, 0x20000, 0x400000),
            ("ota_1", 0, 0x11, 0x420000, 0x400000),
            ("history", 1, 0x82, 0x820000, 0x300000),
        ]
        self.write_partition_binary(entries)
        with self.assertRaisesRegex(ValueError, "binary partition history disagrees"):
            provenance.collect(self.root, SHA)
        self.write_partition_binary()
        partitions = self.root / "firmware/partitions.csv"
        partitions.write_text(partitions.read_text().replace(
            "history,data,spiffs,0x820000,0x400000",
            "history,data,spiffs,0x820000,0x300000"), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "binary partition history disagrees"):
            provenance.collect(self.root, SHA)

    def test_rejects_partition_binary_checksum_padding_and_flags(self):
        path = self.build / "partition_table/partition-table.bin"
        valid = path.read_bytes()
        broken = bytearray(valid)
        broken[12] ^= 1
        path.write_bytes(broken)
        with self.assertRaisesRegex(ValueError, "MD5 checksum"):
            provenance.collect(self.root, SHA)
        path.write_bytes(valid[:-32])
        # Removing a padding entry leaves a valid table; removing the MD5
        # record itself must instead be rejected.
        path.write_bytes(valid[:6 * 32] + b"\\xff" * (0xC00 - 6 * 32))
        with self.assertRaisesRegex(ValueError, "missing MD5"):
            provenance.collect(self.root, SHA)
        path.write_bytes(valid[:7 * 32] + b"X" + valid[7 * 32 + 1:])
        with self.assertRaisesRegex(ValueError, "non-padding"):
            provenance.collect(self.root, SHA)
        modified = bytearray(valid)
        struct.pack_into("<I", modified, 28, 1)  # NVS flags.
        modified[6 * 32 + 16:7 * 32] = hashlib.md5(modified[:6 * 32]).digest()
        path.write_bytes(modified)
        with self.assertRaisesRegex(ValueError, "flags"):
            provenance.collect(self.root, SHA)


if __name__ == "__main__":
    unittest.main()
