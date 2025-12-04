# -*- coding: utf-8 -*-

#!/usr/bin/env python3
"""
AlignImagesToLog.py

For each indexed PLT image, find the nearest entry in the PLT data log
and attach depth (and optionally other sensor values).

Relies on:
  - plt_config.yaml (paths, drop_detection, image_indexing, alignment)
  - ImageIndex.csv produced by ImageIndexer.py
"""

import sys
from io import StringIO
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
dd_cfg = CONFIG.get("drop_detection", {})
ix_cfg = CONFIG.get("image_indexing", {})
align_cfg = CONFIG.get("alignment", {})

# Log / data file
data_path = Path(paths_cfg["data_file"]).expanduser()

# Where to save the aligned table (and where to default index locations)
output_dir = Path(paths_cfg.get("output_dir", HERE / "output")).expanduser()
output_dir.mkdir(parents=True, exist_ok=True)

# -------------------------------------------------------------------
# Resolve image index CSV path
# Priority:
#   1) alignment.image_index_csv
#   2) paths.image_index_csv
#   3) image_indexing.index_csv
#   4) output_dir / "ImageIndex.csv"
# -------------------------------------------------------------------
image_index_csv_cfg = (
    align_cfg.get("image_index_csv")
    or paths_cfg.get("image_index_csv")
    or ix_cfg.get("index_csv")
    or (output_dir / "ImageIndex.csv")
)
image_index_csv = Path(image_index_csv_cfg).expanduser()

# -------------------------------------------------------------------
# Resolve aligned CSV output path
# Priority:
#   1) alignment.output_aligned_csv
#   2) paths.aligned_index_csv
#   3) output_dir / "ImageIndex_aligned.csv"
# -------------------------------------------------------------------
aligned_csv_cfg = (
    align_cfg.get("output_aligned_csv")
    or paths_cfg.get("aligned_index_csv")
    or (output_dir / "ImageIndex_aligned.csv")
)
aligned_csv_path = Path(aligned_csv_cfg).expanduser()

# Matching parameters
max_time_delta_s = float(align_cfg.get("max_time_delta_s", 2.0))

# Column names / formats
log_timestamp_col = dd_cfg.get("timestamp_column", "Timestamp")
log_depth_col = dd_cfg.get("depth_column", "Depth")
log_ts_format = dd_cfg.get("timestamp_format", None)
skip_header_lines = int(dd_cfg.get("skip_header_lines", 0))

img_timestamp_col = ix_cfg.get("timestamp_column", "timestamp")
img_timestamp_raw_col = ix_cfg.get("timestamp_raw_column", "timestamp_raw")
img_ts_format = ix_cfg.get("timestamp_format", None)  # allow None = infer

# -------------------------------------------------------------------
# Load and prepare log (data_53.csv)
# -------------------------------------------------------------------

print(f"Reading log file: {data_path}")

with open(data_path, "r") as f:
    lines = f.readlines()

if skip_header_lines > 0:
    lines = lines[skip_header_lines:]

clean_text = "".join(lines)
log_df = pd.read_csv(StringIO(clean_text))

# Parse timestamps in log
log_df[log_timestamp_col] = pd.to_datetime(
    log_df[log_timestamp_col],
    format=log_ts_format,
)

log_df = log_df.sort_values(log_timestamp_col).reset_index(drop=True)

print(f"Log rows: {len(log_df)}")
print(
    "Log time span:",
    log_df[log_timestamp_col].iloc[0],
    "to",
    log_df[log_timestamp_col].iloc[-1],
)

# -------------------------------------------------------------------
# Load and prepare image index
# -------------------------------------------------------------------

print(f"Reading image index: {image_index_csv}")
img_df = pd.read_csv(image_index_csv)

# -------------------------------------------------------------------
# Ensure image timestamps are proper datetimes
#   Strategy:
#   1. If the configured 'timestamp' column exists, try to ensure it's datetime.
#   2. If that fails or doesn't exist, parse from 'timestamp_raw_column'.
#   3. If a specific format is given in YAML, use it; otherwise let pandas infer.
# -------------------------------------------------------------------


def parse_image_times_from_series(series, fmt):
    """Helper: safely parse a timestamp series with optional format."""
    if fmt:
        parsed = pd.to_datetime(series, format=fmt, errors="coerce")
        # If this yielded nothing but NaT, fall back to infer
        if parsed.notna().sum() == 0:
            print(
                "  Warning: image timestamp format from YAML did not match; "
                "falling back to automatic parsing."
            )
            parsed = pd.to_datetime(series, errors="coerce")
    else:
        parsed = pd.to_datetime(series, errors="coerce")
    return parsed


if img_timestamp_col in img_df.columns:
    # If it's not already datetime, try to parse it
    if not pd.api.types.is_datetime64_any_dtype(img_df[img_timestamp_col]):
        print(
            f"Parsing image timestamps from existing "
            f"'{img_timestamp_col}' column..."
        )
        parsed = parse_image_times_from_series(
            img_df[img_timestamp_col],
            img_ts_format,
        )

        # If still all NaT, try from raw column instead
        if parsed.notna().sum() == 0 and img_timestamp_raw_col in img_df.columns:
            print(
                f"  Existing '{img_timestamp_col}' not usable; "
                f"parsing from '{img_timestamp_raw_col}' instead..."
            )
            parsed = parse_image_times_from_series(
                img_df[img_timestamp_raw_col],
                img_ts_format,
            )

        img_df[img_timestamp_col] = parsed
else:
    # No parsed timestamp column; must build it from the raw EXIF column
    if img_timestamp_raw_col not in img_df.columns:
        raise RuntimeError(
            f"Image index is missing both '{img_timestamp_col}' and "
            f"'{img_timestamp_raw_col}' columns."
        )
    print(f"Parsing image timestamps from '{img_timestamp_raw_col}'...")
    img_df[img_timestamp_col] = parse_image_times_from_series(
        img_df[img_timestamp_raw_col],
        img_ts_format,
    )

# Drop images without valid timestamps
before = len(img_df)
img_df = img_df.dropna(subset=[img_timestamp_col]).copy()
after = len(img_df)
if after < before:
    print(f"Dropped {before - after} images with invalid timestamps.")

# If *still* no valid timestamps, bail out gracefully
if len(img_df) == 0:
    print("ERROR: No images with valid timestamps remain after parsing.")
    print("       Please inspect ImageIndex.csv to verify the timestamp strings,")
    print("       and check 'image_indexing.timestamp_format' in plt_config.yaml.")
    sys.exit(1)

img_df = img_df.sort_values(img_timestamp_col).reset_index(drop=True)

print(f"Images with valid timestamps: {len(img_df)}")
print(
    "Image time span:",
    img_df[img_timestamp_col].iloc[0],
    "to",
    img_df[img_timestamp_col].iloc[-1],
)

# -------------------------------------------------------------------
# Align images to nearest log record
# -------------------------------------------------------------------

print("Aligning images to log (nearest time)...")

aligned = pd.merge_asof(
    img_df,
    log_df,
    left_on=img_timestamp_col,
    right_on=log_timestamp_col,
    direction="nearest",
)

# Compute absolute time offset between image and matched log record
aligned["time_diff_s"] = (
    (aligned[img_timestamp_col] - aligned[log_timestamp_col])
    .abs()
    .dt.total_seconds()
)

# Flag "good" matches
aligned["matched"] = aligned["time_diff_s"] <= max_time_delta_s

# Rename depth column to make it clear this comes from the log
aligned["depth_m"] = aligned[log_depth_col]

# -------------------------------------------------------------------
# Save result
# -------------------------------------------------------------------

aligned.to_csv(aligned_csv_path, index=False)
print(f"Saved aligned index to: {aligned_csv_path}")

# Simple summary
n_images = len(aligned)
n_matched = int(aligned["matched"].sum())
print(f"Total images:   {n_images}")
print(f"Matched (<= {max_time_delta_s:.1f} s): {n_matched}")
print(f"Unmatched:      {n_images - n_matched}")
