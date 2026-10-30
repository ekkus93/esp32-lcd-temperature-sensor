#!/usr/bin/env python3
"""Generate reproducible, explicitly non-release firmware/web build provenance.

This is an offline build-artifact checker. It never flashes devices, reads
credentials, or claims that host/CI verification qualifies a hardware release.
"""
import argparse
import csv
import hashlib
import json
import re
from pathlib import Path

PINNED_IDF = "fff9895c82d744c7237be8847347bdd1b07c6643"
FLASH_BYTES = 16 * 1024 * 1024
PARTITION_TABLE_END = 0x9000
EXPECTED_IMAGES = {
    "bootloader/bootloader.bin": 0x0,
    "partition_table/partition-table.bin": 0x8000,
    "temperature_monitor.bin": 0x20000,
}


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(65536), b""):
            sha.update(chunk)
    return sha.hexdigest()


def regular_under(root, relative):
    """Reject absolute paths, traversal, symlinks and missing build inputs."""
    relative = Path(relative)
    if relative.is_absolute() or not relative.parts or ".." in relative.parts:
        raise ValueError("unsafe artifact path")
    current = root
    for part in relative.parts:
        current = current / part
        if current.is_symlink():
            raise ValueError("symlinked artifact path")
    if not current.is_file():
        raise ValueError("missing regular artifact")
    return current


def slots_from_csv(path):
    """Validate the entire 16 MiB flash layout, not only the two OTA rows."""
    partitions = {}
    with path.open(newline="", encoding="utf-8") as source:
        for row in csv.reader(source):
            if not row or row[0].strip().startswith("#"):
                continue
            if len(row) < 5:
                raise ValueError("malformed partition row")
            name, kind, subtype, offset_text, size_text = (
                field.strip() for field in row[:5]
            )
            if not name or name in partitions or kind not in ("app", "data"):
                raise ValueError("duplicate or invalid partition definition")
            try:
                offset, size = int(offset_text, 0), int(size_text, 0)
            except ValueError as exc:
                raise ValueError("invalid partition offset or size") from exc
            alignment = 0x10000 if kind == "app" else 0x1000
            if (offset < PARTITION_TABLE_END or size <= 0 or
                    offset % alignment or size % alignment or
                    offset + size > FLASH_BYTES):
                raise ValueError("partition outside flash or misaligned")
            partitions[name] = {
                "type": kind, "subtype": subtype, "offset": offset, "size": size
            }
    required = {
        "nvs": ("data", "nvs"),
        "otadata": ("data", "ota"),
        "phy_init": ("data", "phy"),
        "ota_0": ("app", "ota_0"),
        "ota_1": ("app", "ota_1"),
        "history": ("data", "spiffs"),
    }
    for name, expected in required.items():
        if name not in partitions or (
            partitions[name]["type"], partitions[name]["subtype"]
        ) != expected:
            raise ValueError("missing or invalid required partition")
    ordered = sorted(partitions.values(), key=lambda entry: entry["offset"])
    for previous, current in zip(ordered, ordered[1:]):
        if previous["offset"] + previous["size"] > current["offset"]:
            raise ValueError("overlapping flash partitions")
    slots = {
        name: {"offset": partitions[name]["offset"],
               "size": partitions[name]["size"]}
        for name in ("ota_0", "ota_1")
    }
    first, second = slots["ota_0"], slots["ota_1"]
    if (first["size"] != second["size"] or
            first["offset"] + first["size"] > second["offset"]):
        raise ValueError("invalid or overlapping OTA slots")
    return slots, partitions

def collect(root, commit):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("source commit must be an exact lowercase SHA-1")
    build = root / "firmware" / "build"
    web = root / "web"
    dist = web / "dist"
    partitions = regular_under(root, "firmware/partitions.csv")
    lock = regular_under(root, "web/package-lock.json")
    package = regular_under(root, "web/package.json")
    cmake = regular_under(root, "firmware/CMakeLists.txt")
    flash_args = regular_under(build, "flasher_args.json")
    args = json.loads(flash_args.read_text(encoding="utf-8"))
    files = args.get("flash_files")
    if not isinstance(files, dict) or not files:
        raise ValueError("missing flasher_args flash_files mapping")
    images = []
    paths = set()
    for offset_text, name in files.items():
        if not isinstance(name, str) or not isinstance(offset_text, str):
            raise ValueError("invalid flash mapping")
        try:
            offset = int(offset_text, 0)
        except ValueError as exc:
            raise ValueError("invalid flash offset") from exc
        if offset < 0 or name in paths:
            raise ValueError("duplicate or invalid flash mapping")
        paths.add(name)
        file = regular_under(build, name)
        size = file.stat().st_size
        if size <= 0:
            raise ValueError("empty flash image")
        images.append({"offset": offset, "path": name, "size": size,
                       "sha256": digest(file)})
    if not set(EXPECTED_IMAGES).issubset(paths):
        raise ValueError("required bootloader/partition/app image missing")
    for name, expected_offset in EXPECTED_IMAGES.items():
        if next(image["offset"] for image in images if image["path"] == name) != expected_offset:
            raise ValueError("flash image offset disagrees with pinned partition layout")
    images.sort(key=lambda item: item["offset"])
    for previous, current in zip(images, images[1:]):
        if previous["offset"] + previous["size"] > current["offset"]:
            raise ValueError("overlapping flash images")
    slots, layout = slots_from_csv(partitions)
    if slots["ota_0"]["offset"] != EXPECTED_IMAGES["temperature_monitor.bin"]:
        raise ValueError("application flash offset disagrees with ota_0")
    app = next(image for image in images if image["path"] == "temperature_monitor.bin")
    if app["size"] > slots["ota_0"]["size"]:
        raise ValueError("application exceeds an OTA slot")
    for image in images:
        if image["offset"] + image["size"] > FLASH_BYTES:
            raise ValueError("flash image exceeds physical flash size")
        if image["path"] == "temperature_monitor.bin":
            continue
        if any(
            image["offset"] < part["offset"] + part["size"] and
            image["offset"] + image["size"] > part["offset"]
            for part in layout.values()
        ):
            raise ValueError("non-application flash image overlaps partition")
        if (image["offset"] < PARTITION_TABLE_END and
                image["offset"] + image["size"] > 0x8000 and
                image["path"] != "partition_table/partition-table.bin"):
            raise ValueError("flash image overlaps partition table")
        if (image["path"] == "partition_table/partition-table.bin" and
                image["offset"] + image["size"] > PARTITION_TABLE_END):
            raise ValueError("partition table image exceeds reserved space")
    if not dist.is_dir() or dist.is_symlink():
        raise ValueError("missing or symlinked web dist directory")
    assets = []
    for item in sorted(dist.rglob("*")):
        if item.is_symlink():
            raise ValueError("symlink in web dist")
        if item.is_file():
            relative = item.relative_to(dist).as_posix()
            assets.append({"path": relative, "size": item.stat().st_size,
                           "sha256": digest(item)})
        elif not item.is_dir():
            raise ValueError("unexpected web asset type")
    if not assets:
        raise ValueError("web dist is empty")
    match = re.search(r"project\(temperature_monitor VERSION ([0-9]+\.[0-9]+\.[0-9]+)\)",
                      cmake.read_text(encoding="utf-8"))
    if not match:
        raise ValueError("firmware version unavailable")
    firmware_version = match.group(1)
    web_version = json.loads(package.read_text(encoding="utf-8"))["version"]
    lock_version = json.loads(lock.read_text(encoding="utf-8"))["version"]
    if firmware_version != web_version or web_version != lock_version:
        raise ValueError("firmware/web/lock versions disagree")
    checksummed = [
        ("firmware/build/" + image["path"], image["sha256"]) for image in images
    ]
    checksummed += [
        ("web/dist/" + asset["path"], asset["sha256"]) for asset in assets
    ]
    checksummed += [
        ("firmware/build/flasher_args.json", digest(flash_args)),
        ("firmware/partitions.csv", digest(partitions)),
        ("web/package-lock.json", digest(lock)),
        ("web/package.json", digest(package)),
    ]
    manifest = {
        "schema_version": 1,
        "qualification": "CI_BUILD_ONLY_NOT_RELEASE_APPROVED",
        "source_commit": commit,
        "esp_idf_commit": PINNED_IDF,
        "firmware_version": firmware_version,
        "web_version": web_version,
        "ota_slots": slots,
        "flash_size_bytes": FLASH_BYTES,
        "partition_layout": layout,
        "application_bytes": app["size"],
        "ota_headroom_bytes": slots["ota_0"]["size"] - app["size"],
        "flash_images": images,
        "web_assets": assets,
        "web_lock_sha256": digest(lock),
        "partition_csv_sha256": digest(partitions),
        "flasher_args_sha256": digest(flash_args),
    }
    return manifest, sorted(checksummed)


def write_provenance(root, commit, output):
    manifest, checksums = collect(root, commit)
    output.mkdir(parents=True, exist_ok=True)
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    (output / "SHA256SUMS.txt").write_text(
        "".join(f"{sha}  {name}\n" for name, sha in checksums), encoding="utf-8"
    )
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True, help="exact source SHA from CI")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()
    root = args.root.resolve()
    output = args.output or root / "firmware/build/provenance"
    try:
        manifest = write_provenance(root, args.commit, output)
    except (ValueError, OSError, KeyError, TypeError, json.JSONDecodeError) as exc:
        parser.exit(1, f"provenance verification failed: {exc}\n")
    print(f"CI-only provenance verified: {manifest['source_commit']}, "
          f"{manifest['application_bytes']} application bytes; NOT release approved")


if __name__ == "__main__":
    main()
