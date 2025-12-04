# -*- coding: utf-8 -*-


"""
GenerateRunDrops.py

Stage 5A of the PLT processing pipeline.

For a given run suffix (e.g. "50"), this script:

  1. Locates the run directory: drop_root / "data_<suffix>"
     e.g. drops/data_50

  2. Loads the corresponding drops summary:
       drops_summary_<suffix>.csv
     from paths.output_dir, using the drop_detection.drop_summary_filename
     base name from plt_config.yaml.

  3. For each DropNN directory inside that run directory:
       drops/data_50/Drop01
       drops/data_50/Drop02
       ...
     it:

       - Copies the entire contents of pltfilter.run_template_dir
         into the DropNN directory.

       - Reads DropNN_images.csv and writes an images_fullpath.txt
         file listing the full_path of each image (one per line).

       - Writes a drop_metadata.yaml file using the row from
         drops_summary_<suffix>.csv matching drop_id == NN.

Usage (terminal):
    python GenerateRunDrops.py 50
    python GenerateRunDrops.py plt_config.yaml 50

Usage (Spyder):
    runfile('GenerateRunDrops.py', args='50', wdir='...')

Notes:
  - This DOES NOT modify any C++ code or run scripts yet.
  - It simply prepares the per-drop directories to be "RunDrop-ready".
"""

import sys
from pathlib import Path
import shutil
import re

import yaml
import pandas as pd

# Column names (consistent with DropDetect & drops_summary_XX.csv)
DROP_ID_COL = "drop_id"


# ----------------------------------------------------------------------
# Argument parsing
# ----------------------------------------------------------------------

def parse_args(argv):
    """
    Parse command-line / Spyder-style args.

    Patterns:
      []                        -> ("plt_config.yaml", None)
      ["50"]                    -> ("plt_config.yaml", "50")
      ["config.yaml"]           -> ("config.yaml", None)
      ["config.yaml", "50"]     -> ("config.yaml", "50")
    """
    if len(argv) == 0:
        return "plt_config.yaml", None

    if len(argv) == 1:
        arg0 = argv[0]
        if arg0.lower().endswith((".yaml", ".yml")):
            return arg0, None
        else:
            return "plt_config.yaml", arg0

    # len >= 2
    return argv[0], argv[1]


# ----------------------------------------------------------------------
# Config / path helpers
# ----------------------------------------------------------------------

def load_config(config_path: Path) -> dict:
    """Load the global PLT configuration YAML."""
    if not config_path.exists():
        raise FileNotFoundError(f"Config file not found: {config_path}")
    with config_path.open("r") as f:
        return yaml.safe_load(f)


def resolve_run_paths(cfg: dict, run_suffix: str):
    """
    Using YAML + run_suffix, resolve:

      - run_dir: drop_root / "data_<suffix>"
      - run_template_dir: pltfilter.run_template_dir
      - drops_summary_path: drops_summary_<suffix>.csv in output_dir
    """
    paths_cfg = cfg.get("paths", {})
    dd_cfg = cfg.get("drop_detection", {})
    pltfilter_cfg = cfg.get("pltfilter", {})

    drop_root = Path(paths_cfg["drop_root"]).expanduser()
    run_template_dir = Path(pltfilter_cfg["run_template_dir"]).expanduser()
    output_dir = Path(paths_cfg["output_dir"]).expanduser()

    run_label = f"data_{run_suffix}"
    run_dir = drop_root / run_label

    # Build drops_summary_<suffix>.csv name in a way consistent with DropDetect.py
    summary_base = dd_cfg.get("drop_summary_filename", "drops_summary.csv")
    p = Path(summary_base)
    # Strip trailing _NN if present in the stem to avoid double suffixing
    clean_stem = re.sub(r"_\d+$", "", p.stem)
    summary_name = f"{clean_stem}_{run_suffix}{p.suffix}"
    drops_summary_path = output_dir / summary_name

    return run_dir, run_template_dir, drops_summary_path


# ----------------------------------------------------------------------
# Core helpers
# ----------------------------------------------------------------------

def copy_run_template(run_template_dir: Path, drop_dir: Path):
    """
    Copy all files/dirs from run_template_dir into drop_dir.

    Existing subdirs with the same name will be removed and replaced.
    Existing files with the same name will be overwritten.
    """
    if not run_template_dir.exists():
        # Graceful fallback: just warn and return
        print(f"  WARNING: RunTemplate directory not found: {run_template_dir}")
        print("           Skipping template copy for this drop.")
        return

    for item in run_template_dir.iterdir():
        dest = drop_dir / item.name
        if item.is_dir():
            if dest.exists():
                shutil.rmtree(dest)
            shutil.copytree(item, dest)
        else:
            shutil.copy2(item, dest)



def build_images_list(drop_dir: Path, subdf: pd.DataFrame):
    """
    Write a simple images_fullpath.txt in drop_dir using the 'full_path' column.

    Assumes subdf has a 'full_path' column containing absolute paths.
    """
    if "full_path" not in subdf.columns:
        raise KeyError(
            f"'full_path' column not found in per-drop images CSV: {drop_dir}"
        )

    out_path = drop_dir / "images_fullpath.txt"
    with out_path.open("w") as f:
        for p in subdf["full_path"]:
            f.write(str(p) + "\n")

    print(f"  Wrote image list to: {out_path}")


def write_drop_metadata(drop_dir: Path,
                        drop_id: int,
                        run_suffix: str,
                        meta_row: pd.Series):
    """
    Write a drop_metadata.yaml into drop_dir.

    meta_row comes from drops_summary_<suffix>.csv and is expected to have:
      - start_time
      - end_time
      - duration_s
      - start_idx
      - end_idx
      - max_depth_m
    """
    meta = {
        "drop_id": int(drop_id),
        "run_suffix": str(run_suffix),
        "data_file": f"data_{run_suffix}.csv",
        "start_time": str(meta_row.get("start_time")),
        "end_time": str(meta_row.get("end_time")),
        "duration_s": float(meta_row.get("duration_s", 0.0)),
        "start_idx": int(meta_row.get("start_idx", -1)),
        "end_idx": int(meta_row.get("end_idx", -1)),
        "max_depth_m": float(meta_row.get("max_depth_m", float("nan"))),
    }

    out_path = drop_dir / "drop_metadata.yaml"
    with out_path.open("w") as f:
        yaml.safe_dump(meta, f, sort_keys=False)

    print(f"  Wrote drop metadata to: {out_path}")


# ----------------------------------------------------------------------
# Per-run processing
# ----------------------------------------------------------------------

def process_run(cfg: dict, run_suffix: str):
    """
    Main worker for a single data_<suffix> run.
    """
    run_dir, run_template_dir, drops_summary_path = resolve_run_paths(cfg, run_suffix)

    print(f"\n=== GenerateRunDrops for data_{run_suffix} ===")
    print(f"Run directory:     {run_dir}")
    print(f"RunTemplate dir:   {run_template_dir}")
    print(f"Drops summary CSV: {drops_summary_path}")

    if not run_dir.exists():
        raise FileNotFoundError(f"Run directory not found: {run_dir}")

    if not drops_summary_path.exists():
        raise FileNotFoundError(f"Drops summary not found: {drops_summary_path}")

    if not run_template_dir.exists():
        print(f"WARNING: RunTemplate dir does not exist: {run_template_dir}")
        print("         Will still build images_fullpath.txt and drop_metadata.yaml,\n"
              "         but no template files will be copied into DropNN folders.")

    # Load drops summary (for per-drop metadata)
    drops_summary_df = pd.read_csv(drops_summary_path)

    # Loop over DropNN subdirectories
    for drop_dir in sorted(run_dir.iterdir()):
        if not drop_dir.is_dir():
            continue

        name = drop_dir.name  # e.g. "Drop01"
        m = re.match(r"Drop(\d+)$", name)
        if not m:
            continue

        drop_id = int(m.group(1))
        drop_label = name

        images_csv = drop_dir / f"{drop_label}_images.csv"
        if not images_csv.exists():
            print(f"  WARNING: {images_csv} not found; skipping {drop_label}.")
            continue

        subdf = pd.read_csv(images_csv)

        print(f"\nProcessing {drop_label} (drop_id={drop_id})...")

        # 1. Copy RunTemplate into this DropNN directory (if available)
        copy_run_template(run_template_dir, drop_dir)

        # 2. Build images_fullpath.txt
        build_images_list(drop_dir, subdf)

        # 3. Look up metadata for this drop_id and write drop_metadata.yaml
        meta_row = drops_summary_df.loc[
            drops_summary_df[DROP_ID_COL] == drop_id
        ]
        if not meta_row.empty:
            write_drop_metadata(drop_dir, drop_id, run_suffix, meta_row.iloc[0])
        else:
            print("  WARNING: No matching drop_id in drops_summary; "
                  "metadata file not written.")



# ----------------------------------------------------------------------
# Main entry point
# ----------------------------------------------------------------------

def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]

    config_path_str, run_suffix = parse_args(argv)
    config_path = Path(config_path_str)

    if run_suffix is None:
        print("Please provide a run suffix (e.g., '50').\n"
              "Usage:\n"
              "  GenerateRunDrops.py 50\n"
              "  GenerateRunDrops.py plt_config.yaml 50")
        return

    cfg = load_config(config_path)
    process_run(cfg, run_suffix)


if __name__ == "__main__":
    main()
