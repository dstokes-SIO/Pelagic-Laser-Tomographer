# -*- coding: utf-8 -*-

#!/usr/bin/env python3
"""
AlignImagesToLogs_Batch.py

Batch version of AlignImagesToLog.py.

For each PLT data log file (data_*.csv) in the same directory as the
configured paths.data_file, this script:

  1. Loads the PLT data log and parses its timestamps.
  2. Computes the log time span.
  3. Filters the global ImageIndex.csv to only those images whose EXIF
     timestamps fall within [log_start - buffer, log_end + buffer],
     where 'buffer' is a small time margin.
  4. Aligns those images to the log using nearest-neighbor merge_asof.
  5. Computes time_diff_s and matched flags.
  6. Writes a per-log aligned CSV into the output directory, e.g.:

        ImageIndex_aligned_53.csv   (for data_53.csv)
        ImageIndex_aligned_52.csv   (for data_52.csv)
        ...

Relies on:
  - plt_config.yaml (paths, drop_detection, image_indexing, alignment)
  - ImageIndex.csv produced by ImageIndexer.py

Notes:
  - This script does NOT modify the original AlignImagesToLog.py.
  - It shares the same timestamp parsing and alignment logic for consistency.
"""

import sys
import re
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

# Base data_file (used to infer logs directory)
data_path = Path(paths_cfg["data_file"]).expanduser()
logs_dir = data_path.parent

# Where to save aligned tables (and where to default index locations)
output_dir = Path(paths_cfg.get("output_dir", HERE / "output")).expanduser()
output_dir.mkdir(parents=True, exist_ok=True)

# -------------------------------------------------------------------
# Resolve image index CSV path (global ImageIndex from ImageIndexer.py)
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

# Matching parameters
max_time_delta_s = float(align_cfg.get("max_time_delta_s", 2.0))

# Time buffer around each log's time span for image pre-filtering (seconds)
# You can add 'image_time_buffer_s' under 'alignment' in YAML if desired.
image_time_buffer_s = float(align_cfg.get("image_time_buffer_s", 600.0))  # default 10 minutes

# Column names / formats (shared with AlignImagesToLog.py)
log_timestamp_col = dd_cfg.get("timestamp_column", "Timestamp")
log_depth_col = dd_cfg.get("depth_column", "Depth")
log_ts_format = dd_cfg.get("timestamp_format", None)
skip_header_lines = int(dd_cfg.get("skip_header_lines", 0))

img_timestamp_col = ix_cfg.get("timestamp_column", "timestamp")
img_timestamp_raw_col = ix_cfg.get("timestamp_raw_column", "timestamp_raw")
img_ts_format = ix_cfg.get("timestamp_format", None)  # allow None = infer


# -------------------------------------------------------------------
# Helper: parse timestamps with optional fixed format
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


# -------------------------------------------------------------------
# Load and prepare global image index ONCE
# -------------------------------------------------------------------

print(f"Reading global image index: {image_index_csv}")
img_df = pd.read_csv(image_index_csv)

# Ensure image timestamps are proper datetimes (same logic as AlignImagesToLog.py)
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

# If *still* no valid timestamps, bail out
if len(img_df) == 0:
    print("ERROR: No images with valid timestamps remain after parsing.")
    print("       Please inspect ImageIndex.csv to verify timestamp strings,")
    print("       and check 'image_indexing.timestamp_format' in plt_config.yaml.")
    sys.exit(1)

img_df = img_df.sort_values(img_timestamp_col).reset_index(drop=True)

print(f"Global images with valid timestamps: {len(img_df)}")
print(
    "Global image time span:",
    img_df[img_timestamp_col].iloc[0],
    "to",
    img_df[img_timestamp_col].iloc[-1],
)


# -------------------------------------------------------------------
# Main batch logic
# -------------------------------------------------------------------

def align_for_log(log_path: Path, img_df_all: pd.DataFrame):
    """
    Align images to a single log file (log_path) using nearest time.

    This function:
      - Reads the log file (respecting skip_header_lines and timestamp format).
      - Computes log_start / log_end.
      - Filters img_df_all to a buffered time window.
      - Performs merge_asof alignment.
      - Computes time_diff_s and matched.
      - Renames depth column to 'depth_m'.
      - Masks log columns for unmatched rows.
      - Writes a per-log aligned CSV in output_dir.
    """
    print("\n" + "-" * 70)
    print(f"Processing log file: {log_path}")

    # Load and clean log file similarly to AlignImagesToLog.py
    with open(log_path, "r") as f:
        lines = f.readlines()

    if skip_header_lines > 0:
        lines = lines[skip_header_lines:]

    clean_text = "".join(lines)
    log_df = pd.read_csv(StringIO(clean_text))

    # Parse log timestamps
    log_df[log_timestamp_col] = pd.to_datetime(
        log_df[log_timestamp_col],
        format=log_ts_format,
    )
    log_df = log_df.sort_values(log_timestamp_col).reset_index(drop=True)

    print(f"Log rows: {len(log_df)}")
    if len(log_df) == 0:
        print("  WARNING: Log is empty after parsing. Skipping.")
        return

    log_start = log_df[log_timestamp_col].iloc[0]
    log_end = log_df[log_timestamp_col].iloc[-1]
    print("Log time span:", log_start, "to", log_end)

    # Compute buffered time window for images
    buffer = pd.Timedelta(seconds=image_time_buffer_s)
    t_min = log_start - buffer
    t_max = log_end + buffer

    # Filter images to this time window
    img_subset = img_df_all[
        (img_df_all[img_timestamp_col] >= t_min)
        & (img_df_all[img_timestamp_col] <= t_max)
    ].copy()

    print(
        f"Images in buffered time window "
        f"([{t_min}, {t_max}]): {len(img_subset)}"
    )

    if len(img_subset) == 0:
        print("  No images found in this time window. Skipping alignment.")
        return

    img_subset = img_subset.sort_values(img_timestamp_col).reset_index(drop=True)

    # Align images to log (nearest time)
    print("Aligning images to this log (nearest time)...")

    aligned = pd.merge_asof(
        img_subset,
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

    # ----------------------------------------------------------
    # Mask log-derived columns for unmatched rows
    # ----------------------------------------------------------
    # Columns coming from the PLT log (not EXIF/image-side).
    log_columns = [
        log_timestamp_col,
        "Milliseconds",
        "Pressure",
        "Depth",
        "Water_Temperature",
        "Device_Temperature",
        "Acceleration_X",
        "Acceleration_Y",
        "Acceleration_Z",
        "Magnetic_X",
        "Magnetic_Y",
        "Magnetic_Z",
        "Gyroscope_X",
        "Gyroscope_Y",
        "Gyroscope_Z",
        "Controller_Volts",
        "Controller_Percent",
        "Main_Volts",
        "Main_Percent",
    ]

    # Keep only those that actually exist after merge
    log_columns = [c for c in log_columns if c in aligned.columns]

    # Also treat depth_m as log-derived
    if "depth_m" in aligned.columns and "depth_m" not in log_columns:
        log_columns.append("depth_m")

    # Null out log-side columns for unmatched rows
    mask_unmatched = ~aligned["matched"]
    if mask_unmatched.any() and log_columns:
        aligned.loc[mask_unmatched, log_columns] = pd.NA

    # Determine output filename based on log filename
    # e.g., data_53.csv -> ImageIndex_aligned_53.csv
    stem = log_path.stem  # e.g., "data_53"
    m = re.search(r"(\d+)$", stem)
    if m:
        suffix = m.group(1)
        out_name = f"ImageIndex_aligned_{suffix}.csv"
    else:
        out_name = f"ImageIndex_aligned_{stem}.csv"

    aligned_csv_path = output_dir / out_name
    aligned.to_csv(aligned_csv_path, index=False)

    # Simple summary
    n_images = len(aligned)
    n_matched = int(aligned["matched"].sum())
    print(f"Saved aligned index to: {aligned_csv_path}")
    print(f"Total images (subset):   {n_images}")
    print(f"Matched (<= {max_time_delta_s:.1f} s): {n_matched}")
    print(f"Unmatched:               {n_images - n_matched}")


def main():
    # Discover all data_*.csv logs in the logs_dir
    log_files = sorted(logs_dir.glob("data_*.csv"))

    if not log_files:
        print(f"No data_*.csv files found in {logs_dir}")
        sys.exit(0)

    print(f"\nFound {len(log_files)} log file(s) in {logs_dir}:")
    for lf in log_files:
        print("  -", lf.name)

    # Loop over each log and perform alignment
    for log_path in log_files:
        align_for_log(log_path, img_df_all=img_df)


if __name__ == "__main__":
    main()
# -*- coding: utf-8 -*-
