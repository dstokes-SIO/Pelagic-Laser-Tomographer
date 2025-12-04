# -*- coding: utf-8 -*-

#!/usr/bin/env python3
"""
RunPLTFilter.py

Stage 6 of the PLT processing pipeline.

Goal
----
For a given run_suffix (e.g. '50'), this script:

  - Finds the per-drop folders created by GenerateRunDrops.py:
      drops/data_50/Drop01, Drop02, ...

  - For each DropNN folder:
      * Reads images_fullpath.txt (full paths of ARWs for that drop).
      * Reads drop_metadata.yaml (drop_id, run_suffix, data_file, start_idx, etc).
      * Stages images into a local DropImages directory via symlinks
        with sequential names PLT000001.ARW, PLT000002.ARW, ...
      * Constructs a pltfilter command line (mirroring the old bin/filter
        script) using parameters from plt_config.yaml:pltfilter.
      * Calls the C++ pltfilter binary via subprocess.run.
      * Concatenates DotsPerImage/*.dots.csv into a single AllDots.csv
        (per drop), similar to bin/filter.

Inputs
------
  - plt_config.yaml
      paths:
        data_file        # used to find logs directory
        drop_root        # root for drops (e.g. .../PLT/drops)
      pltfilter:
        binary           # path to pltfilter binary
        mask_file        # path to Mask.tiff (optional)
        threads, verbose, median_window, thresholds, etc.

  - drops/<run_dir>/DropNN/images_fullpath.txt
  - drops/<run_dir>/DropNN/drop_metadata.yaml

Outputs
-------
  Per-drop, under drops/data_XX/DropNN/:

    DropImages/            # staged PLT000001.ARW, ...
    Results/
      DotsPerImage/*.dots.csv
      FilteredImages/      (if enabled)
      AllDots.csv
      DotsRadiusHistogram.csv
      DotsAreaHistogram.csv
      DotsDepthHistogram.csv
"""

import sys
import subprocess
from pathlib import Path
from typing import List

import yaml
import pandas as pd  # only used for optional inspection if we add later


# -------------------------------------------------------------------
# YAML loading
# -------------------------------------------------------------------

HERE = Path(__file__).resolve().parent
CONFIG_PATH = HERE / "plt_config.yaml"


def load_config(config_path: Path) -> dict:
    if not config_path.exists():
        raise FileNotFoundError(f"Config file not found: {config_path}")
    with config_path.open("r") as f:
        return yaml.safe_load(f)


# -------------------------------------------------------------------
# Helper: parse run_suffix from argv (Spyder-friendly)
# -------------------------------------------------------------------

def parse_run_suffix(argv: list) -> str | None:
    """
    Extract the first non-flag argument as run_suffix, e.g.:

        runfile('RunPLTFilter.py', args='50', wdir='...')

    yields sys.argv == ['RunPLTFilter.py', '50'].
    """
    if not argv:
        return None
    for a in argv:
        if not a.startswith("-"):
            return a
    return None


# -------------------------------------------------------------------
# Helper: stage images (Python replacement for bin/extract)
# -------------------------------------------------------------------

def stage_images_for_drop(
    drop_dir: Path,
    image_paths: List[str],
    name: str,
) -> Path:
    """
    Create a DropImages/<name> folder inside drop_dir and symlink each
    image listed in image_paths as PLT000001.ARW, PLT000002.ARW, ...

    Returns:
        src_folder (Path): the folder to be passed as SRC_FOLDER to pltfilter.
    """
    # e.g. drops/data_50/Drop01/DropImages/Drop01_data_50
    drop_images_root = drop_dir / "DropImages"
    src_folder = drop_images_root / name
    src_folder.mkdir(parents=True, exist_ok=True)

    for i, src_str in enumerate(image_paths, start=1):
        src = Path(src_str).expanduser()
        if not src.exists():
            raise FileNotFoundError(f"Image file not found: {src}")

        # Match old PLT naming pattern: PLT000001.ARW, PLT000002.ARW, ...
        dst = src_folder / f"PLT{i:06d}{src.suffix}"

        # If symlink/file exists, remove it first to be safe
        if dst.exists() or dst.is_symlink():
            dst.unlink()

        # Symlink to avoid copying many GB of data
        dst.symlink_to(src)

    return src_folder


# -------------------------------------------------------------------
# Helper: build pltfilter command line (Python version of bin/filter)
# -------------------------------------------------------------------

def build_pltfilter_cmd(
    plt_cfg: dict,
    src_folder: Path,
    dst_folder: Path,
    log_file: Path | None,
    log_start: int | None,
) -> list[str]:
    """
    Build the pltfilter command line based on plt_cfg and log information.

    Mirrors the behavior of bin/filter, but pulls parameters from YAML.
    """
    # Binary path
    binary = plt_cfg.get("binary") or plt_cfg.get("pltfilter_binary") or "pltfilter"
    binary_path = Path(binary).expanduser()

    # Ensure dst_folder exists
    dst_folder.mkdir(parents=True, exist_ok=True)

    # Subfolders / files (same naming as bin/filter)
    dots_folder = dst_folder / "DotsPerImage"
    filtered_images_folder = dst_folder / "FilteredImages"
    all_dots_file = dst_folder / "AllDots.csv"
    radius_hist_file = dst_folder / "DotsRadiusHistogram.csv"
    area_hist_file = dst_folder / "DotsAreaHistogram.csv"
    depth_hist_file = dst_folder / "DotsDepthHistogram.csv"

    dots_folder.mkdir(parents=True, exist_ok=True)
    filtered_images_folder.mkdir(parents=True, exist_ok=True)

    # Build options list
    opts: list[str] = []

    # Threads / verbosity
    threads = int(plt_cfg.get("threads", 0))
    if threads > 0:
        opts += ["--threads", str(threads)]
    if bool(plt_cfg.get("verbose", False)):
        opts.append("--verbose")

    # Mask
    mask_file = plt_cfg.get("mask_file") or plt_cfg.get("default_mask")
    if mask_file:
        mask_path = Path(mask_file).expanduser()
        opts += ["--mask", str(mask_path)]

    # Log
    if log_file is not None:
        opts += ["--logfile", str(log_file)]
        if log_start is not None:
            opts += ["--logstart", str(int(log_start))]

    # Output toggles
    save_dots = bool(plt_cfg.get("save_dots", True))
    save_radius_hist = bool(plt_cfg.get("save_radius_histogram", True))
    save_area_hist = bool(plt_cfg.get("save_area_histogram", True))
    save_depth_hist = bool(plt_cfg.get("save_depth_histogram", True))
    save_filtered_images = bool(plt_cfg.get("save_filtered_images", False))

    if save_dots:
        opts += ["--savedots", str(dots_folder)]
    if save_area_hist:
        opts += ["--saveareahistogram", str(area_hist_file)]
    if save_depth_hist:
        opts += ["--savedepthhistogram", str(depth_hist_file)]
    if save_radius_hist:
        opts += ["--saveradiushistogram", str(radius_hist_file)]
    if save_filtered_images:
        opts += ["--saveimages", str(filtered_images_folder)]

    # Threshold settings
    adaptive_window = int(plt_cfg.get("adaptive_threshold_window", 31))
    simple_threshold = int(plt_cfg.get("simple_threshold", 0))
    adaptive_bias = float(plt_cfg.get("adaptive_threshold_bias", -9.0))
    adaptive_type = str(plt_cfg.get("adaptive_threshold_type", "mean"))

    if adaptive_window > 0:
        opts += ["--thresholdwindow", str(adaptive_window)]
        opts += ["--thresholdbias", str(adaptive_bias)]
        opts += ["--thresholdtype", adaptive_type]
    elif simple_threshold != 0:
        opts += ["--threshold", str(simple_threshold)]
    else:
        # Explicitly disable thresholding, as in bin/filter
        opts += ["--threshold", "0"]

    # Median window
    median_window = int(plt_cfg.get("median_window", 3))
    if median_window != 0:
        opts += ["--medianwindow", str(median_window)]
    else:
        opts += ["--medianwindow", "0"]

    # Dilation
    dilations = int(plt_cfg.get("dilation_count", 1))
    if dilations != 0:
        opts += ["--dilations", str(dilations)]
    else:
        opts += ["--dilations", "0"]

    # Dot filters
    opts += ["--mindotradius", str(int(plt_cfg.get("min_dot_radius", 1)))]
    opts += ["--maxdotradius", str(int(plt_cfg.get("max_dot_radius", 10)))]
    opts += ["--mindotarea", str(int(plt_cfg.get("min_dot_area", 1)))]
    opts += ["--maxdotarea", str(int(plt_cfg.get("max_dot_area", 100)))]

    # Geometry / false Z / depth hist
    false_z = float(plt_cfg.get("false_z_spacing", 25.0))
    pixels_per_cm = float(plt_cfg.get("pixels_per_cm", 77.165))
    depth_bin = float(plt_cfg.get("depth_histogram_bucket_width", 0.1))

    opts += ["--falsezspacing", str(false_z)]
    opts += ["--pixelspercm", str(pixels_per_cm)]
    opts += ["--depthhistogramspacing", str(depth_bin)]

    # Final command: [pltfilter, OPTIONS..., SRC_FOLDER]
    cmd = [str(binary_path), *opts, str(src_folder)]
    return cmd


# -------------------------------------------------------------------
# Helper: provide a cleaned log file for pltfilter
# -------------------------------------------------------------------

def get_log_file_for_pltfilter(cfg: dict, run_suffix: str | None) -> Path:
    """
    Return a log CSV suitable for pltfilter.

    If drop_detection.skip_header_lines > 0, this creates (once) a cleaned copy
    of data_<suffix>.csv with those header lines removed and returns its path.
    Otherwise it just returns the original data_<suffix>.csv.

    This keeps the row indices consistent with what DropDetect / Align* used.
    """
    paths_cfg = cfg.get("paths", {})
    dd_cfg = cfg.get("drop_detection", {})

    base_data_path = Path(paths_cfg["data_file"]).expanduser()
    logs_dir = base_data_path.parent

    # Determine which data_XX.csv we’re using
    if run_suffix:
        data_path = logs_dir / f"data_{run_suffix}.csv"
    else:
        data_path = base_data_path

    skip_header = int(dd_cfg.get("skip_header_lines", 0))

    # If no funny header lines, just use the original log
    if skip_header <= 0:
        return data_path

    # Put cleaned copies in the output dir
    output_dir = Path(paths_cfg.get("output_dir", HERE / "output")).expanduser()
    output_dir.mkdir(parents=True, exist_ok=True)

    clean_name = f"{data_path.stem}_clean_for_pltfilter{data_path.suffix}"
    clean_path = output_dir / clean_name

    # Only build it once
    if not clean_path.exists():
        with data_path.open("r") as f:
            lines = f.readlines()

        if len(lines) <= skip_header:
            raise RuntimeError(
                f"Log file {data_path} has fewer lines than "
                f"skip_header_lines={skip_header}"
            )

        with clean_path.open("w") as f:
            f.writelines(lines[skip_header:])

        print(f"  Wrote cleaned log CSV for pltfilter: {clean_path}")

    return clean_path


# -------------------------------------------------------------------
# Helper: concatenate per-image .dots.csv into AllDots.csv
# -------------------------------------------------------------------

def concatenate_dots(dst_folder: Path) -> Path:
    """
    Concatenate DotsPerImage/*.dots.csv into AllDots.csv, similar to bin/filter.

    Strategy:
      - Find all *.dots.csv in DotsPerImage (sorted).
      - Use the header line from the first file.
      - For subsequent files, skip the header and append only data lines.

    Returns:
      Path to AllDots.csv
    """
    dots_folder = dst_folder / "DotsPerImage"
    all_dots_file = dst_folder / "AllDots.csv"

    dots_files = sorted(dots_folder.glob("*.dots.csv"))
    if not dots_files:
        print(f"  WARNING: No .dots.csv files found in {dots_folder}")
        return all_dots_file

    with all_dots_file.open("w", encoding="utf-8") as out_f:
        header_written = False
        for i, fpath in enumerate(dots_files):
            with fpath.open("r", encoding="utf-8") as in_f:
                lines = in_f.readlines()
            if not lines:
                continue

            if not header_written:
                # Write header from first file
                out_f.write(lines[0])
                header_written = True
                data_lines = lines[1:]
            else:
                # Skip header line on subsequent files
                data_lines = lines[1:]

            for line in data_lines:
                # bin/filter used `sed '/"X/d'` to drop lines containing "X".
                # In practice, skipping the header achieves the same effect.
                out_f.write(line)

    print(f"  Concatenated dots into: {all_dots_file}")
    return all_dots_file


# -------------------------------------------------------------------
# Main processing for a single run_suffix
# -------------------------------------------------------------------

def process_run(cfg: dict, run_suffix: str | None) -> None:
    paths_cfg = cfg.get("paths", {})
    plt_cfg = cfg.get("pltfilter", {})

    drop_root = Path(paths_cfg.get("drop_root", HERE / "drops")).expanduser()

    # Determine which "data_XX" directory to process
    if run_suffix is not None:
        run_dir_name = f"data_{run_suffix}"
        run_dir = drop_root / run_dir_name
        title_suffix = f"data_{run_suffix}"
    else:
        # Fall back to the base data_file from YAML
        base_data_path = Path(paths_cfg["data_file"]).expanduser()
        run_dir_name = base_data_path.stem  # e.g., 'data_53'
        run_dir = drop_root / run_dir_name
        title_suffix = run_dir_name

    print(f"\n=== RunPLTFilter for {title_suffix} ===")
    print(f"Drop root: {drop_root}")
    print(f"Run directory: {run_dir}")

    if not run_dir.exists():
        print(f"  ERROR: Run directory does not exist: {run_dir}")
        return

    # Build / locate the log file that pltfilter should use
    log_file_for_run = get_log_file_for_pltfilter(cfg, run_suffix)

    # Find all DropNN directories
    drop_dirs = sorted(
        d for d in run_dir.iterdir()
        if d.is_dir() and d.name.lower().startswith("drop")
    )

    if not drop_dirs:
        print(f"  No DropNN directories found under {run_dir}")
        return

    for drop_dir in drop_dirs:
        drop_label = drop_dir.name  # e.g., 'Drop01'
        print(f"\nProcessing {drop_label} in {run_dir_name}...")

        # Read metadata
        meta_path = drop_dir / "drop_metadata.yaml"
        if not meta_path.exists():
            print(f"  WARNING: drop_metadata.yaml not found for {drop_label}; skipping.")
            continue

        with meta_path.open("r") as f:
            meta = yaml.safe_load(f)

        drop_id = meta.get("drop_id")
        data_file_name = meta.get("data_file")  # e.g. 'data_50.csv' (not strictly needed here)
        log_start = meta.get("start_idx")

        # Read images_fullpath.txt
        images_list_path = drop_dir / "images_fullpath.txt"
        if not images_list_path.exists():
            print(f"  WARNING: images_fullpath.txt not found for {drop_label}; skipping.")
            continue

        with images_list_path.open("r", encoding="utf-8") as f:
            image_paths = [line.strip() for line in f.readlines() if line.strip()]

        if not image_paths:
            print(f"  WARNING: No images listed for {drop_label}; skipping.")
            continue

        # Stage images (Python replacement for bin/extract)
        derived_name = f"{drop_label}_{title_suffix}"
        src_folder = stage_images_for_drop(
            drop_dir=drop_dir,
            image_paths=image_paths,
            name=derived_name,
        )
        print(f"  Staged {len(image_paths)} images into: {src_folder}")

        # Destination folder for results
        dst_folder = drop_dir / "Results"

        # Build and run pltfilter command
        cmd = build_pltfilter_cmd(
            plt_cfg=plt_cfg,
            src_folder=src_folder,
            dst_folder=dst_folder,
            log_file=log_file_for_run,
            log_start=log_start,
        )

        print("  Running pltfilter with command:")
        print("   ", " ".join(cmd))

        try:
            subprocess.run(cmd, check=True)
        except subprocess.CalledProcessError as e:
            print(f"  ERROR: pltfilter failed for {drop_label}: {e}")
            continue

        # Concatenate dots CSVs into AllDots.csv
        concatenate_dots(dst_folder)


# -------------------------------------------------------------------
# Entry point
# -------------------------------------------------------------------

def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]

    run_suffix = parse_run_suffix(argv)
    cfg = load_config(CONFIG_PATH)

    process_run(cfg, run_suffix)


if __name__ == "__main__":
    main()
# -*- coding: utf-8 -*-
