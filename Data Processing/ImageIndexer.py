#!/usr/bin/env python3
"""
ImageIndexer.py

Scan a directory tree for PLT ARW images, read EXIF timestamps with exiftool,
and write a CSV index for later alignment with the PLT drops.

Configuration comes from plt_config.yaml (see 'paths' and 'image_indexing' sections).
"""

import subprocess
import json
from pathlib import Path

import pandas as pd
import yaml

# -------------------------------------------------------------------
# Load YAML configuration
# -------------------------------------------------------------------

HERE = Path(__file__).resolve().parent
CONFIG_PATH = HERE / "plt_config.yaml"

with open(CONFIG_PATH, "r") as f:
    CONFIG = yaml.safe_load(f)

paths_cfg = CONFIG.get("paths", {})
img_cfg = CONFIG.get("image_indexing", {})   # <--- updated section name

# -------------------------------------------------------------------
# Resolve paths from YAML
# -------------------------------------------------------------------

# Where to put outputs
output_dir = Path(paths_cfg.get("output_dir", HERE / "output")).expanduser()
output_dir.mkdir(parents=True, exist_ok=True)

# Root directory containing raw PLT images.
# Priority:
#   1) paths.image_root (global convention)
#   2) image_indexing.root_dir (script-specific)
#   3) fall back to HERE (script directory) if nothing is set
image_root = paths_cfg.get("image_root")
if image_root is None:
    image_root = img_cfg.get("root_dir", HERE)
image_root = Path(image_root).expanduser()

# Where to write the index CSV.
# Priority:
#   1) paths.image_index_csv
#   2) image_indexing.index_csv
#   3) default to output_dir / "ImageIndex.csv"
index_csv_path = paths_cfg.get("image_index_csv")
if index_csv_path is None:
    index_csv_path = img_cfg.get("index_csv", output_dir / "ImageIndex.csv")
index_path = Path(index_csv_path).expanduser()

overwrite_index = bool(img_cfg.get("overwrite_index", True))
if index_path.exists() and not overwrite_index:
    print(f"Index file already exists and overwrite_index is False: {index_path}")
    print("Skipping re-indexing.")
    raise SystemExit(0)

# -------------------------------------------------------------------
# EXIF / imaging configuration
# -------------------------------------------------------------------

# Full path to exiftool binary (from YAML)
exiftool_path = img_cfg.get("exiftool_path", "exiftool")

# EXIF tags used for time
timestamp_tag = img_cfg.get("timestamp_tag", "DateTimeOriginal")
subsec_tag = img_cfg.get("subsec_tag", "SubSecTimeOriginal")

# Format string for parsing the EXIF timestamp into pandas datetime
# Typical: "2023:05:26 09:50:21"
timestamp_format = img_cfg.get("timestamp_format", "%Y:%m:%d %H:%M:%S")

# -------------------------------------------------------------------
# Scan for ARW files
# -------------------------------------------------------------------

print(f"Scanning for ARW files under: {image_root}")
arw_files = sorted(image_root.rglob("*.ARW"))

if not arw_files:
    print(f"No ARW files found under: {image_root}")
    raise SystemExit(0)

print(f"Found {len(arw_files)} images.")

# -------------------------------------------------------------------
# Call exiftool for all files at once
# -------------------------------------------------------------------

# We call exiftool with -json and the explicit file list
cmd = [exiftool_path, "-json"] + [str(p) for p in arw_files]

try:
    output = subprocess.check_output(cmd)
except FileNotFoundError:
    print(f"ERROR: exiftool not found at '{exiftool_path}'.")
    print("Update 'image_indexing.exiftool_path' in plt_config.yaml to point to the full path,")
    print("for example: /opt/homebrew/bin/exiftool")
    raise
except subprocess.CalledProcessError as e:
    print("ERROR running exiftool:")
    print(e)
    raise

meta_list = json.loads(output.decode("utf-8"))

# -------------------------------------------------------------------
# Build index DataFrame
# -------------------------------------------------------------------

rows = []
for meta in meta_list:
    # exiftool always returns a "SourceFile" entry
    src = meta.get("SourceFile", "")
    src_path = Path(src)

    # Only keep ARW files under image_root
    if src_path.suffix.lower() != ".arw":
        continue
    try:
        rel_path = src_path.relative_to(image_root)
    except ValueError:
        # Not under the desired root (exiftool can wander if given a broad path)
        continue

    # Core EXIF fields
    ts_raw = meta.get(timestamp_tag)
    subsec = meta.get(subsec_tag)

    rows.append({
        "filename": src_path.name,
        "rel_path": str(rel_path),
        "full_path": str(src_path),
        "timestamp_raw": ts_raw,
        "subsec_raw": subsec,
    })

df = pd.DataFrame(rows).sort_values("full_path").reset_index(drop=True)

# Parse timestamps using the EXIF format
df["timestamp"] = pd.to_datetime(
    df["timestamp_raw"],
    format=timestamp_format,
    errors="coerce"
)

# -------------------------------------------------------------------
# Save CSV and print a quick preview
# -------------------------------------------------------------------

df.to_csv(index_path, index=False)
print(f"Saved image index to: {index_path}")
print(df.head())
