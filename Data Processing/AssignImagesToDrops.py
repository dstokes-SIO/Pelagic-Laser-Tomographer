# -*- coding: utf-8 -*-

# -*- coding: utf-8 -*-

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AssignImagesToDrops.py

Stage 4 of the PLT processing pipeline.

Goal
----
Take:
  - The detected drops (drops_summary*.csv from DropDetect.py), and
  - The image–log alignment table (ImageIndex_aligned_*.csv from
    AlignImagesToLog.py or AlignImagesToLogs_Batch.py),

and assign each *matched* image to a drop based on the log timestamp falling
inside [start_time, end_time] for that drop.

Per-log behaviour
-----------------
You can run this per log "suffix" (e.g., 50, 51, 52, 53), expecting:

  - drops_summary_50.csv in output_dir
  - ImageIndex_aligned_50.csv in output_dir

and writing:

  - drop_image_assignments_50.csv in output_dir
  - drops/data_50/DropXX/DropXX_images.csv
  - images_per_drop_50.png

Inputs (from YAML: plt_config.yaml)
-----------------------------------
  cfg["paths"]["output_dir"]
  cfg["paths"]["drop_root"]
  cfg["paths"]["aligned_index_csv"]      # fallback for single-run case
  cfg["drop_detection"]["drop_summary_filename"]  # base name for summary

Files (per log_suffix, if provided)
-----------------------------------
  - drops_summary_<suffix>.csv (in output_dir)
  - ImageIndex_aligned_<suffix>.csv     (in output_dir)

If no suffix is given, fall back to:
  - drops_summary.csv
  - ImageIndex_aligned.csv

Outputs
-------
  1. Master CSV in output_dir:
       drop_image_assignments_<suffix>.csv (or drop_image_assignments.csv)

     This includes *all* images from the aligned CSV, with:
       - drop_id assigned where possible
       - drop_id = NaN for images not assigned to any drop

  2. Per-drop CSVs in drop_root (per log run):
       drops/data_<suffix>/Drop01/Drop01_images.csv
       drops/data_<suffix>/Drop02/Drop02_images.csv
       ...

     These include only images with a valid drop_id.

  3. Simple QC plot in output_dir:
       images_per_drop_<suffix>.png

Notes
-----
- Membership is determined using the *log* timestamp column ("Timestamp"),
  not the EXIF timestamp.
- Only images with matched == True are considered for assignment, but
  unmatched images are still retained in the master assignments CSV
  with drop_id = NaN.
"""

import sys
from pathlib import Path

import yaml
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt


# ----------------------------------------------------------------------
# Column-name conventions – matched to your existing files
# ----------------------------------------------------------------------

# drops_summary.csv column names (confirmed from your file)
DROP_ID_COL = "drop_id"
DROP_START_COL = "start_time"
DROP_END_COL = "end_time"
DROP_START_IDX_COL = "start_idx"      # not used yet, but available
DROP_END_IDX_COL = "end_idx"
DROP_DURATION_COL = "duration_s"
DROP_MAX_DEPTH_COL = "max_depth_m"

# ImageIndex_aligned.csv column names (from your file)
IMAGE_FILENAME_COL = "filename"
IMAGE_RELPATH_COL = "rel_path"
IMAGE_FULLPATH_COL = "full_path"
IMAGE_TIMESTAMP_RAW_COL = "timestamp_raw"   # EXIF raw, not directly used here
IMAGE_SUBSEC_RAW_COL = "subsec_raw"         # not used here
IMAGE_IMAGE_TIME_COL = "timestamp"          # image timestamp (EXIF)
IMAGE_LOG_TIME_COL = "Timestamp"            # log timestamp (for membership)
IMAGE_DEPTH_COL = "Depth"                   # depth from log
IMAGE_TIME_DELTA_COL = "time_diff_s"        # difference image vs log (sec)
IMAGE_MATCHED_COL = "matched"               # boolean (or equivalent)

# Base master assignment output filename (we add suffix when needed)
DEFAULT_ASSIGNMENTS_BASE = "drop_image_assignments"


# ----------------------------------------------------------------------
# YAML loading
# ----------------------------------------------------------------------

def load_config(config_path: str) -> dict:
    """Load the global PLT configuration YAML."""
    config_path = Path(config_path)
    if not config_path.exists():
        raise FileNotFoundError(f"Config file not found: {config_path}")
    with config_path.open("r") as f:
        cfg = yaml.safe_load(f)
    return cfg


# ----------------------------------------------------------------------
# Data loading helpers
# ----------------------------------------------------------------------

def make_suffix_str(run_suffix):
    """Return a printable suffix string like '_53' or '' if None."""
    if run_suffix is None:
        return ""
    return f"_{run_suffix}"


def load_drops_summary(cfg: dict, run_suffix=None) -> pd.DataFrame:
    """
    Load drops_summary[_<suffix>].csv using paths from the YAML.

    Uses:
      - cfg["paths"]["output_dir"]
      - cfg["drop_detection"]["drop_summary_filename"] as a base name

    If run_suffix is not None, we look for:
      base -> base with '_<suffix>' inserted before '.csv'
      e.g., 'drops_summary.csv' -> 'drops_summary_53.csv'
    """
    output_dir = Path(cfg["paths"]["output_dir"])
    base_name = cfg["drop_detection"]["drop_summary_filename"]  # e.g., 'drops_summary.csv'
    base_path = output_dir / base_name

    if run_suffix is not None:
        # Insert suffix before extension
        stem = base_path.stem          # e.g., 'drops_summary'
        suffix_str = make_suffix_str(run_suffix)  # e.g., '_53'
        drop_summary_path = output_dir / f"{stem}{suffix_str}.csv"
    else:
        drop_summary_path = base_path

    if not drop_summary_path.exists():
        raise FileNotFoundError(f"drops_summary not found: {drop_summary_path}")

    df = pd.read_csv(drop_summary_path)

    # Parse start_time / end_time as datetime
    df[DROP_START_COL] = pd.to_datetime(df[DROP_START_COL])
    df[DROP_END_COL] = pd.to_datetime(df[DROP_END_COL])

    return df


def load_aligned_images(cfg: dict, run_suffix=None) -> pd.DataFrame:
    """
    Load ImageIndex_aligned[_<suffix>].csv.

    If run_suffix is None, uses:
      - cfg["paths"]["aligned_index_csv"]

    If run_suffix is not None, uses:
      - cfg["paths"]["output_dir"] / f"ImageIndex_aligned_<suffix>.csv"
    """
    output_dir = Path(cfg["paths"]["output_dir"])

    if run_suffix is not None:
        aligned_path = output_dir / f"ImageIndex_aligned_{run_suffix}.csv"
    else:
        aligned_path = Path(cfg["paths"]["aligned_index_csv"])

    if not aligned_path.exists():
        raise FileNotFoundError(f"Aligned image–log CSV not found: {aligned_path}")

    df = pd.read_csv(aligned_path)

    # Parse log timestamp into datetime for membership tests
    df[IMAGE_LOG_TIME_COL] = pd.to_datetime(df[IMAGE_LOG_TIME_COL])

    # Ensure 'matched' is a boolean mask we can rely on
    if IMAGE_MATCHED_COL in df.columns:
        col = df[IMAGE_MATCHED_COL]
        # Robust coercion to boolean:
        if col.dtype == bool:
            matched_mask = col
        elif np.issubdtype(col.dtype, np.number):
            matched_mask = col.astype(bool)
        else:
            # Handle string-like representations ("True", "False", "1", "0", etc.)
            matched_mask = col.astype(str).str.lower().isin(["true", "1", "yes"])
        df[IMAGE_MATCHED_COL] = matched_mask
    else:
        # If the column is missing for some reason, treat all as matched
        df[IMAGE_MATCHED_COL] = True

    return df


# ----------------------------------------------------------------------
# Core assignment logic (time-based membership using log timestamp)
# ----------------------------------------------------------------------

def assign_images_to_drops(drops_df: pd.DataFrame,
                           images_df: pd.DataFrame) -> pd.DataFrame:
    """
    Assign each image to a drop based on the log timestamp.

    Rules:
      - Use the log timestamp column (IMAGE_LOG_TIME_COL, i.e. 'Timestamp').
      - Only images with IMAGE_MATCHED_COL == True are *eligible* for assignment.
      - For each drop (drop_id, start_time, end_time), assign eligible images whose
        log timestamp lies within [start_time, end_time].
      - Images that do not fall in any drop window (or are unmatched) get drop_id = NaN.

    Returns:
      A DataFrame 'assignments' that is a copy of images_df with an added 'drop_id' column.
      Unmatched / unassigned images are retained with drop_id = NaN.
    """
    # Work on a copy so we don't mutate caller's DataFrame
    images = images_df.copy()

    # Ensure log time is datetime (in case caller hasn't done this)
    images[IMAGE_LOG_TIME_COL] = pd.to_datetime(images[IMAGE_LOG_TIME_COL])

    # Initialize drop_id column as NaN (unassigned)
    images[DROP_ID_COL] = np.nan

    # Boolean mask: which images are eligible for drop assignment?
    if IMAGE_MATCHED_COL in images.columns:
        eligible = images[IMAGE_MATCHED_COL] == True
    else:
        eligible = pd.Series(True, index=images.index)

    # Sort by time for consistency
    images.sort_values(IMAGE_LOG_TIME_COL, inplace=True)
    drops_sorted = drops_df.sort_values(DROP_START_COL)

    # Loop over drops (drops are few, so this is fine)
    for _, drop_row in drops_sorted.iterrows():
        drop_id = drop_row[DROP_ID_COL]
        t_start = drop_row[DROP_START_COL]
        t_end = drop_row[DROP_END_COL]

        # Time window mask
        in_time_window = (images[IMAGE_LOG_TIME_COL] >= t_start) & \
                         (images[IMAGE_LOG_TIME_COL] <= t_end)

        # Only assign to images that are:
        #   - eligible (matched == True)
        #   - currently unassigned
        unassigned = images[DROP_ID_COL].isna()
        assign_mask = eligible & unassigned & in_time_window

        images.loc[assign_mask, DROP_ID_COL] = drop_id

    # 'images' is now our master assignment table
    assignments = images

    return assignments


# ----------------------------------------------------------------------
# Output helpers
# ----------------------------------------------------------------------

def write_master_assignments(assignments: pd.DataFrame, cfg: dict, run_suffix=None) -> Path:
    """
    Write the master assignment CSV into cfg["paths"]["output_dir"].

    Includes all images, with drop_id set where applicable.

    If run_suffix is not None, appends '_<suffix>' to the base filename.
    """
    output_dir = Path(cfg["paths"]["output_dir"])
    output_dir.mkdir(parents=True, exist_ok=True)

    suffix_str = make_suffix_str(run_suffix)
    fname = f"{DEFAULT_ASSIGNMENTS_BASE}{suffix_str}.csv"
    assignments_path = output_dir / fname
    assignments.to_csv(assignments_path, index=False)
    print(f"Wrote master assignments to: {assignments_path}")

    return assignments_path


def write_per_drop_files(assignments: pd.DataFrame, cfg: dict, run_suffix=None) -> None:
    """
    For each drop_id present in the assignments, write a per-drop CSV into
    cfg["paths"]["drop_root"].

    If run_suffix is provided, structure is:

        drop_root/data_<suffix>/Drop01/Drop01_images.csv
        drop_root/data_<suffix>/Drop02/Drop02_images.csv
        ...

    If run_suffix is None (single-run legacy case):

        drop_root/Drop01/Drop01_images.csv
        ...
    """
    drop_root = Path(cfg["paths"]["drop_root"])
    drop_root.mkdir(parents=True, exist_ok=True)

    assigned = assignments.dropna(subset=[DROP_ID_COL]).copy()
    if assigned.empty:
        print("No assigned images to write per-drop files.")
        return

    # Ensure drop_id is integer (NaNs were removed above)
    assigned[DROP_ID_COL] = assigned[DROP_ID_COL].astype(int)

    # Per-run subdirectory for multi-log case
    if run_suffix is not None:
        run_dir = drop_root / f"data_{run_suffix}"
    else:
        run_dir = drop_root

    run_dir.mkdir(parents=True, exist_ok=True)

    for drop_id, subdf in assigned.groupby(DROP_ID_COL):
        drop_label = f"Drop{drop_id:02d}"
        drop_dir = run_dir / drop_label
        drop_dir.mkdir(parents=True, exist_ok=True)

        drop_csv_path = drop_dir / f"{drop_label}_images.csv"

        # For now, write all columns; we can trim later if desired.
        subdf.to_csv(drop_csv_path, index=False)
        print(f"Wrote per-drop images for drop_id={drop_id} to: {drop_csv_path}")


# ----------------------------------------------------------------------
# Simple QC: images per drop
# ----------------------------------------------------------------------

def plot_images_per_drop(assignments: pd.DataFrame, cfg: dict, run_suffix=None) -> None:
    """
    Simple QC plot: number of images per drop.

    Creates images_per_drop[_<suffix>].png in cfg["paths"]["output_dir"].
    """
    output_dir = Path(cfg["paths"]["output_dir"])
    output_dir.mkdir(parents=True, exist_ok=True)

    assigned = assignments.dropna(subset=[DROP_ID_COL]).copy()
    if assigned.empty:
        print("No assigned images; skipping QC plot.")
        return

    assigned[DROP_ID_COL] = assigned[DROP_ID_COL].astype(int)
    counts = assigned.groupby(DROP_ID_COL)[IMAGE_FILENAME_COL].count()

    plt.figure()
    counts.sort_index().plot(kind="bar")
    plt.xlabel("Drop ID")
    plt.ylabel("Number of images")
    plt.title("Images per Drop")

    suffix_str = make_suffix_str(run_suffix)
    plot_path = output_dir / f"images_per_drop{suffix_str}.png"
    plt.tight_layout()
    plt.savefig(plot_path)
    print(f"Wrote images-per-drop plot to: {plot_path}")
    # If you want interactive display during debugging, you can uncomment:
    # plt.show()


# ----------------------------------------------------------------------
# Quick Diagnostic
# ----------------------------------------------------------------------

def print_debug_summary(drops_df: pd.DataFrame, images_df: pd.DataFrame, run_suffix=None):
    suffix_str = make_suffix_str(run_suffix)
    print("\n=== AssignImagesToDrops: Debug summary "
          f"for run_suffix='{run_suffix}' ===")

    print(f"Number of drops: {len(drops_df)}")
    print("Drops time range:",
          drops_df[DROP_START_COL].min(), "→", drops_df[DROP_END_COL].max())

    print(f"\nTotal images in aligned table: {len(images_df)}")

    if IMAGE_MATCHED_COL in images_df.columns:
        print("matched value_counts:")
        print(images_df[IMAGE_MATCHED_COL].value_counts(dropna=False))

    # Parse times to datetime for the summary (if not already)
    try:
        t_log = pd.to_datetime(images_df[IMAGE_LOG_TIME_COL])
        print("Image log timestamp range:",
              t_log.min(), "→", t_log.max())
    except Exception as e:
        print("Unable to parse IMAGE_LOG_TIME_COL for summary:", e)


# ----------------------------------------------------------------------
# Argument parsing helper
# ----------------------------------------------------------------------

def parse_args(argv):
    """
    Parse command-line-like arguments.

    Accepts:
      - no args:          config='plt_config.yaml', run_suffix=None
      - one arg:
          * if endswith('.yaml' or '.yml'): treat as config path
          * else: treat as run_suffix (config='plt_config.yaml')
      - two or more args:
          * argv[0]: config path
          * argv[1]: run_suffix (string or number)

    Returns:
      (config_path: str, run_suffix: Optional[str])
    """
    if len(argv) == 0:
        return "plt_config.yaml", None

    if len(argv) == 1:
        arg0 = argv[0]
        if arg0.lower().endswith((".yaml", ".yml")):
            return arg0, None
        else:
            # Single arg = run_suffix, default config
            return "plt_config.yaml", arg0

    # len(argv) >= 2
    config_path = argv[0]
    run_suffix = argv[1]
    return config_path, run_suffix


# ----------------------------------------------------------------------
# Main entry point
# ----------------------------------------------------------------------

def main(argv=None):
    """
    Main driver for AssignImagesToDrops.

    Usage examples:
        python AssignImagesToDrops.py
        python AssignImagesToDrops.py plt_config.yaml
        python AssignImagesToDrops.py 53
        python AssignImagesToDrops.py plt_config.yaml 53

    Where '53' is the run_suffix corresponding to:
      - ImageIndex_aligned_53.csv
      - drops_summary_53.csv
    """
    if argv is None:
        argv = sys.argv[1:]

    config_path, run_suffix = parse_args(argv)

    cfg = load_config(config_path)

    drops_df = load_drops_summary(cfg, run_suffix=run_suffix)
    images_df = load_aligned_images(cfg, run_suffix=run_suffix)

    print_debug_summary(drops_df, images_df, run_suffix=run_suffix)

    assignments = assign_images_to_drops(drops_df, images_df)

    write_master_assignments(assignments, cfg, run_suffix=run_suffix)
    write_per_drop_files(assignments, cfg, run_suffix=run_suffix)
    plot_images_per_drop(assignments, cfg, run_suffix=run_suffix)


if __name__ == "__main__":
    main()
