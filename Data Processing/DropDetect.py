# -*- coding: utf-8 -*-

import pandas as pd
import matplotlib.pyplot as plt
import sys
from pathlib import Path
from io import StringIO
import yaml
import re

# -------------------------------------------------------------------
# Load YAML configuration
# -------------------------------------------------------------------

# Assume config file is in the same directory as this script
HERE = Path(__file__).resolve().parent
CONFIG_PATH = HERE / "plt_config.yaml"

with open(CONFIG_PATH, "r") as f:
    CONFIG = yaml.safe_load(f)

paths_cfg = CONFIG.get("paths", {})
dd_cfg = CONFIG.get("drop_detection", {})

# Optional run suffix from command-line arguments (Spyder-compatible)
# Example usage in Spyder:
#   runfile('DropDetect.py', args='53', wdir='...')
# which yields sys.argv == ['DropDetect.py', '53'] inside this script.
if len(sys.argv) > 1:
    _argv = sys.argv[1:]
    # Ignore notebook/debug flags if present; keep the first non-flag as suffix
    run_suffix = None
    for a in _argv:
        if not a.startswith("-"):
            run_suffix = a
            break
else:
    run_suffix = None

# Build suffix string for naming outputs
suffix_str = f"_{run_suffix}" if run_suffix is not None else ""

# Base data log path from YAML
base_data_path = Path(paths_cfg["data_file"]).expanduser()
logs_dir = base_data_path.parent

# If a run_suffix is provided, override to data_<suffix>.csv in the same directory
if run_suffix is not None:
    data_path = logs_dir / f"data_{run_suffix}.csv"
else:
    data_path = base_data_path

print(f"Using data file: {data_path}")

# Ensure output / plots directories exist
output_dir = Path(paths_cfg.get("output_dir", HERE / "output")).expanduser()
plots_dir = Path(paths_cfg.get("plots_dir", output_dir / "plots")).expanduser()
output_dir.mkdir(parents=True, exist_ok=True)
plots_dir.mkdir(parents=True, exist_ok=True)

# -------------------------------------------------------------------
# Read and clean CSV
# -------------------------------------------------------------------

skip_header_lines = int(dd_cfg.get("skip_header_lines", 0))

with open(data_path, "r") as f:
    lines = f.readlines()

if skip_header_lines > 0:
    lines = lines[skip_header_lines:]

clean_text = "".join(lines)
df = pd.read_csv(StringIO(clean_text))

# -------------------------------------------------------------------
# Parse timestamps
# -------------------------------------------------------------------

timestamp_col = dd_cfg.get("timestamp_column", "Timestamp")
depth_col = dd_cfg.get("depth_column", "Depth")
ts_format = dd_cfg.get("timestamp_format", None)

df[timestamp_col] = pd.to_datetime(
    df[timestamp_col],
    format=ts_format,
)

df = df.sort_values(timestamp_col).reset_index(drop=True)

# -------------------------------------------------------------------
# Optional smoothing of Depth to suppress jitter
# -------------------------------------------------------------------

rolling_window = int(dd_cfg.get("rolling_window", 5))
depth = df[depth_col]

if rolling_window > 1:
    depth_smooth = depth.rolling(
        rolling_window,
        center=True,
        min_periods=1
    ).median()
else:
    depth_smooth = depth.copy()

df["Depth_smooth"] = depth_smooth

# -------------------------------------------------------------------
# Detect drops as contiguous segments where depth >= min_drop_depth
# -------------------------------------------------------------------

min_depth = float(dd_cfg.get("min_drop_depth_m", 5.0))
min_duration_s = float(dd_cfg.get("min_drop_duration_s", 20.0))

is_deep = df["Depth_smooth"] >= min_depth

# Label contiguous runs
drop_ids = (is_deep != is_deep.shift(fill_value=False)).cumsum()

drops = []
for gid, group in df[is_deep].groupby(drop_ids[is_deep]):
    t_start = group[timestamp_col].iloc[0]
    t_end = group[timestamp_col].iloc[-1]
    duration_s = (t_end - t_start).total_seconds()

    if duration_s < min_duration_s:
        # too short to be a real drop
        continue

    start_idx = group.index[0]
    end_idx = group.index[-1]
    max_depth = group[depth_col].max()

    drops.append({
        "drop_id": len(drops) + 1,  # 1-based index
        "start_time": t_start,
        "end_time": t_end,
        "duration_s": duration_s,
        "start_idx": start_idx,
        "end_idx": end_idx,
        "max_depth_m": max_depth,
    })

drops_df = pd.DataFrame(drops)

# -------------------------------------------------------------------
# Save a drop summary table
# -------------------------------------------------------------------
if not drops_df.empty:
    summary_base = dd_cfg.get("drop_summary_filename", "drops_summary.csv")
    p_sum = Path(summary_base)
    # Strip any trailing _NN digits from stem to avoid double suffixes
    clean_stem = re.sub(r"_\d+$", "", p_sum.stem)
    summary_name = f"{clean_stem}{suffix_str}{p_sum.suffix}"
    summary_csv_path = output_dir / summary_name

    drops_df.to_csv(summary_csv_path, index=False)
    print(f"\nSaved drop summary to: {summary_csv_path}")
else:
    print("\nNo drops detected; no summary file written.")

# -------------------------------------------------------------------
# Optional debug plot: depth vs time with shaded drops
# -------------------------------------------------------------------

plot_debug = bool(dd_cfg.get("plot_debug", False))
show_plot = bool(dd_cfg.get("show_plot", True))
save_plot = bool(dd_cfg.get("save_plot", False))
plot_base = dd_cfg.get("plot_filename", "drops_debug.png")

p_plot = Path(plot_base)
# Strip any trailing _NN digits from stem before appending suffix
clean_plot_stem = re.sub(r"_\d+$", "", p_plot.stem)
plot_filename = f"{clean_plot_stem}{suffix_str}{p_plot.suffix}"

if plot_debug:
    fig, ax = plt.subplots(figsize=(10, 5))

    # Plot smoothed depth
    ax.plot(df[timestamp_col], df["Depth_smooth"],
            label="Depth (smoothed) [m]")

    # Shade detected drops
    for _, row in drops_df.iterrows():
        ax.axvspan(row["start_time"], row["end_time"],
                   color="orange", alpha=0.2)

    ax.invert_yaxis()
    ax.set_xlabel("Time")
    ax.set_ylabel("Depth [m]")
    ax.set_title("Depth vs Time with Detected Drops")
    ax.legend(loc="best")
    fig.autofmt_xdate()

    if save_plot:
        out_path = plots_dir / plot_filename
        fig.savefig(out_path, dpi=150, bbox_inches="tight")
        print(f"Saved debug plot to: {out_path}")

    if show_plot:
        plt.show()
    else:
        plt.close(fig)

# -------------------------------------------------------------------
# Optional per-drop profile plots (metrics vs depth)
# -------------------------------------------------------------------

plot_profiles = bool(dd_cfg.get("plot_profiles", False))

if plot_profiles and not drops_df.empty:
    profile_metrics = dd_cfg.get(
        "profile_metrics",
        ["Water_Temperature", "Device_Temperature"]
    )
    show_profile_plots = bool(dd_cfg.get("show_profile_plots", True))
    save_profile_plots = bool(dd_cfg.get("save_profile_plots", True))
    max_profiles_to_plot = int(
        dd_cfg.get("max_profiles_to_plot", len(drops_df))
    )

    # Use only metrics that actually exist in the dataframe
    available_metrics = [m for m in profile_metrics if m in df.columns]
    if not available_metrics:
        print("No requested profile metrics found in data columns; "
              "skipping profile plots.")
    else:
        print("\nGenerating per-drop profile plots...")
        for _, row in drops_df.head(max_profiles_to_plot).iterrows():
            drop_id = int(row["drop_id"])
            start_idx = row["start_idx"]
            end_idx = row["end_idx"]

            subset = df.loc[start_idx:end_idx].copy()

            fig, axes = plt.subplots(
                nrows=len(available_metrics),
                ncols=1,
                figsize=(6, 3 * len(available_metrics)),
                sharex=True
            )

            if len(available_metrics) == 1:
                axes = [axes]

            for ax, metric in zip(axes, available_metrics):
                if metric not in subset.columns:
                    continue
                ax.plot(subset[metric], subset[depth_col])
                ax.set_ylabel("Depth [m]")
                ax.set_xlabel(metric)
                ax.invert_yaxis()
                ax.grid(True, alpha=0.3)

            fig.suptitle(f"Drop {drop_id} profiles", fontsize=14)
            fig.tight_layout(rect=[0, 0.03, 1, 0.95])

            if save_profile_plots:
                fname = f"drop_{drop_id:02d}_profiles{suffix_str}.png"
                out_path = plots_dir / fname
                fig.savefig(out_path, dpi=150, bbox_inches="tight")
                print(f"  Saved profile plot for drop {drop_id} to: {out_path}")

            if show_profile_plots:
                plt.show()
            else:
                plt.close(fig)
else:
    if plot_profiles:
        print("plot_profiles=True but no drops detected; "
              "no profile plots will be generated.")
# -*- coding: utf-8 -*-
