[PLT_Processing_User_Guide.md](https://github.com/user-attachments/files/23918088/PLT_Processing_User_Guide.md)
# **PLT Processing User Guide**

## **1. Introduction**

This document describes the processing workflow for the Pelagic Laser Tomographer (PLT) dataset.  
The goal of the pipeline is to convert raw Sony ARW image sequences and associated depth logs into cleaned, standardized **dot table CSV files** for each drop.

The PLT processing pipeline is modular and consists of the following components:

- **Raw data (ARW images + CSV log)**
- **GenerateRunDrops.py** — identifies drops and organizes input files
- **RunPLTFilter.py** — stages images, runs the C++ `pltfilter` algorithm, and produces output dot files
- **Concatenate outputs** — produces `AllDots.csv` for each drop

This guide explains how to run each stage, the required directory structure, and how to interpret the resulting files.

---

## **2. Processing Pipeline Overview**

The system processes a PLT “run” (e.g., `data_50`) consisting of:

- A raw CSV log file: `data_50.csv` generated during PLT operations
- A set of raw ARW images containing several drops (drops = depth cast of PLT from winch or hand held)

The processing steps are:

1. **GenerateRunDrops.py**
   - Reads the raw images
   - Reads the raw log
   - Detects drops and isolates them
   - Produces:
     - One folder per drop
     - `images_fullpath.txt`
     - `drop_metadata.yaml`

2. **RunPLTFilter.py**
   - Reads each drop folder
   - Symlinks all images for the drop into a sequentially numbered directory
   - Cleans the log file (standardizes depth column name)
   - Constructs a `pltfilter` command based on `plt_config.yaml`
   - Calls the C++ `pltfilter` binary
   - Produces:
     - `DotsPerImage/*.dots.csv`
     - Summary histogram files
     - `AllDots.csv`

3. **Downstream analysis** (not covered here)
   - Users may apply their own scripts for density, depth structure, or cross-drop comparison.

---

## **3. Directory Structure**

A typical PLT working directory is organized as follows:

```
PLT/
  plt_config.yaml
  GenerateRunDrops.py
  RunPLTFilter.py
  PLTfilter/
    src/pltfilter
    RunTemplate/Mask.tiff
  drops/
    data_50/
      Drop01/
        images_fullpath.txt
        drop_metadata.yaml
        DropImages/
        Results/
  raw_data_05262023/
    data_50.csv
    ARW image folders
```

Key directories:

- **PLTfilter/**  
  Contains the compiled `pltfilter` executable and mask file.

- **drops/**  
  Contains per-run and per-drop processing outputs.

- **raw_data/**  
  Contains raw ARW image sets and raw log files.

---

## **4. Configuration File: `plt_config.yaml`**

All path definitions and `pltfilter` parameters live in `plt_config.yaml`.  
Important sections are:

### **4.1 paths**
```yaml
paths:
  drop_root: "/path/to/PLT/drops"
  data_file: "/path/to/raw_data/data_50.csv"
  raw_images_root: "/path/to/raw_images"
```

- `drop_root`: Parent folder under which drop folders will be created  
- `data_file`: Raw CSV log for the run  
- `raw_images_root`: Root folder containing ARW images

### **4.2 pltfilter**
Defines how `RunPLTFilter.py` constructs the C++ `pltfilter` command.

Key fields:

```yaml
pltfilter:
  binary: "/path/to/PLTfilter/src/pltfilter"
  mask_file: "/path/to/Mask.tiff"
  median_window: 3
  adaptive_threshold_window: 31
  adaptive_threshold_bias: -9
  dilation_count: 1
  min_dot_radius: 1
  max_dot_radius: 10
  min_dot_area: 1
  max_dot_area: 100
  false_z_spacing: 25.0
  pixels_per_cm: 77.165
  depth_histogram_bucket_width: 0.1
  save_dots: true
```

---

## **5. Running the Pipeline**

### **5.1 Step 1 — Prepare Raw Data**

Ensure that:

- The raw CSV log (e.g., `data_50.csv`) is located where `plt_config.yaml` points.
- ARW image folders are accessible.
- The directory structure is consistent with `GenerateRunDrops.py`.

No additional formatting of raw logs is required.

---

### **5.2 Step 2 — Identify Drops**

To process a run (e.g., run number 50):

```python
runfile("GenerateRunDrops.py", args="50", wdir="...")
```

This script:

- Reads the raw ARW images & timestamps
- Selects frames belonging to each drop
- Creates a directory for each drop:

```
drops/data_50/Drop01
drops/data_50/Drop02
...
```

Each drop folder contains:

- `images_fullpath.txt` — list of ARW file paths  
- `drop_metadata.yaml` — includes:
  - `drop_id`
  - `data_file`
  - `start_idx` (first log row for this drop)

This stage must be run before filtering.

---

### **5.3 Step 3 — Run PLT Filter**

To process all drops in a run:

```python
runfile("RunPLTFilter.py", args="50", wdir="...")
```

The script performs:

1. **Log cleaning**
   - Standardizes the “depth” column name so the C++ code recognizes it.

2. **Symlink image staging**
   - All ARW files for the drop are placed into:

     ```
     DropImages/Drop01_data_50/
       PLT000001.ARW
       PLT000002.ARW
       ...
     ```

3. **Calls the C++ `pltfilter` binary**
   - Uses parameters from the YAML file
   - Writes output to:

     ```
     Results/
       DotsPerImage/
       AllDots.csv
       DotsRadiusHistogram.csv
       DotsAreaHistogram.csv
       DotsDepthHistogram.csv
     ```

Progress and command-line details print to the console.

---

### **5.4 Step 4 — Inspect Output**

Inside each drop’s `Results/` folder:

#### **AllDots.csv**
Combined dot detections for all frames in the drop.  
Columns include:

- X-Pixels  
- Y-Pixels  
- Area  
- Radius  
- Frame number  
- Depth (if logged)

#### **DotsPerImage/**
Individual dot detections per frame.

#### **Histogram files**
Summaries of:

- Dot radii  
- Dot areas  
- Dot depth distribution

---

## **6. Troubleshooting**

### **6.1 pltfilter: column not found: Depth**
Cause: Raw log contains nonstandard column header (e.g., `depth (m)` or `Depth (m)`).

Solution:  
`RunPLTFilter.py` automatically cleans logs.  
Check for:

```
output/data_XX_clean_for_pltfilter.csv
```

Make sure the cleaned file contains a column named exactly:

```
Depth
```

---

### **6.2 pltfilter not found**
Ensure the YAML contains:

```yaml
pltfilter:
  binary: "/Users/.../PLTfilter/src/pltfilter"
```

---

### **6.3 Missing Mask.tiff**
Verify:

```yaml
mask_file: "/Users/.../PLTfilter/RunTemplate/Mask.tiff"
```

---

### **6.4 No DotsPerImage files generated**
Possible causes:

- Mask cropping out entire frame  
- Wrong threshold settings  
- Bad exposure or blank images

Try lowering threshold bias or disabling adaptive thresholding.

---

### **6.5 ARW files not found**
Check the content of:

```
drops/data_XX/DropNN/images_fullpath.txt
```

Paths must be absolute or resolvable.

---

## **7. Notes on Extending the Pipeline**

Advanced users may wish to:

- Adjust detection thresholds in YAML  
- Add new metadata fields  
- Replace `pltfilter` with a GPU version  
- Implement Python-based dot detectors  
- Export PLT dot clouds to MATLAB or NumPy arrays  
- Add automatic QA/QC after filtering  

This guide covers only the canonical workflow.

---

## **8. Summary**

This user guide provides the minimal, operational recipe for running the full PLT processing pipeline:

1. Configure paths in `plt_config.yaml`  
2. Generate drop folders using `GenerateRunDrops.py`  
3. Run filtering using `RunPLTFilter.py`  
4. Inspect `Results/AllDots.csv` and summary files  


