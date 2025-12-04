# -*- coding: utf-8 -*-

#!/usr/bin/env python3
"""
PLTFilterStages.py

Standalone visualizer for PLTfilter-like image processing stages.

Usage (Spyder example)
----------------------
runfile(
    '/Users/siocomputer/Documents/SPYDER/PLT/PLTFilterStages.py',
    args='/Volumes/Xtra/PLT May26/DCIM/100MSDCF/DSC09861.ARW '
         '/Users/siocomputer/Documents/SPYDER/PLT/figures/DSC09861',
    wdir='/Users/siocomputer/Documents/SPYDER/PLT'
)

Arguments:
    1. Required: path to an image (ARW, JPG, PNG, etc.)
    2. Optional: output directory. If omitted, a folder will be created
       next to the image:
           <image_parent>/pltfilter_stages_<stem>
           
           
           runfile(
               '/Users/siocomputer/Documents/SPYDER/PLT/PLTFilterStages.py',
               args='/Volumes/Xtra/PLT May26/DCIM/100MSDCF/DSC09861.ARW '
                    '/Users/siocomputer/Documents/SPYDER/PLT/figures/DSC09861',
               wdir='/Users/siocomputer/Documents/SPYDER/PLT'
           )

This script does NOT call the C++ pltfilter binary. It implements an
approximate version of the C++ pipeline in pure Python/OpenCV to generate
illustrative stage images for documentation / manuscripts.
"""

import sys
from pathlib import Path
from typing import Tuple, Optional

import numpy as np

import cv2

try:
    import rawpy  # for reading RAW (ARW) files
    HAS_RAWPY = True
except ImportError:
    HAS_RAWPY = False


# --------------------------------------------------------------------
# Configuration (keep roughly in sync with pltfilter config)
# --------------------------------------------------------------------

MEDIAN_WINDOW = 3

SIMPLE_THRESHOLD = 0
ADAPTIVE_THRESHOLD_WINDOW = 31
#ADAPTIVE_THRESHOLD_BIAS = -9.0
ADAPTIVE_THRESHOLD_BIAS = -8.9
ADAPTIVE_THRESHOLD_TYPE = "mean"  # "mean" or "gaussian"

DILATION_COUNT = 1

# For dot visualization
MIN_DOT_RADIUS = 1
MAX_DOT_RADIUS = 10
MIN_DOT_AREA = 1
MAX_DOT_AREA = 100

# Mask file (same as C++ pipeline)
MASK_FILE = "/Users/siocomputer/Documents/SPYDER/PLT/PLTfilter/RunTemplate/Mask.tiff"


# --------------------------------------------------------------------
# Argument parsing (Spyder-friendly)
# --------------------------------------------------------------------

def parse_args(argv: list) -> Tuple[Path, Optional[Path]]:
    """
    Parse arguments in a way that works nicely with Spyder's runfile.

    Strategy:
      - Ignore flags starting with '-'.
      - Try to assemble the first existing path by joining non-flag tokens
        with spaces, to handle paths that contain spaces (e.g. 'PLT May26').
      - The remaining non-flag tokens (if any) are joined as the output dir.

    Example argv (from Spyder):

      [
        '"/Volumes/Xtra/PLT',
        'May26/DCIM/100MSDCF/DSC09861.ARW"',
        '"/Users/.../figures/DSC09861"'
      ]

      -> image_path = /Volumes/Xtra/PLT May26/DCIM/100MSDCF/DSC09861.ARW
         output_dir = /Users/.../figures/DSC09861
    """
    # Drop anything that looks like a flag
    non_flags = [a for a in argv if not a.startswith("-")]

    if len(non_flags) == 0:
        raise RuntimeError(
            "PLTFilterStages.py requires at least one argument: the image path.\n"
            "Example in Spyder:\n"
            "  runfile('PLTFilterStages.py', "
            "args='/path/to/image.ARW /optional/output/dir', wdir='...')"
        )

    # Helper: strip leading/trailing quotes
    def strip_quotes(s: str) -> str:
        s = s.strip()
        if (s.startswith('"') and s.endswith('"')) or (s.startswith("'") and s.endswith("'")):
            return s[1:-1]
        return s

    # Try to find an existing image path by progressively joining tokens
    image_path: Optional[Path] = None
    image_tokens_count = 0

    for i in range(1, len(non_flags) + 1):
        candidate = " ".join(non_flags[:i])
        candidate = strip_quotes(candidate)
        candidate_path = Path(candidate).expanduser()
        if candidate_path.exists():
            image_path = candidate_path
            image_tokens_count = i
            break

    if image_path is None:
        # Fall back to first token (will error later if wrong, but with a clear message)
        first = strip_quotes(non_flags[0])
        image_path = Path(first).expanduser()
        image_tokens_count = 1

    # Remaining tokens (if any) form the output directory
    if image_tokens_count < len(non_flags):
        out_dir_str = " ".join(non_flags[image_tokens_count:])
        out_dir_str = strip_quotes(out_dir_str)
        output_dir = Path(out_dir_str).expanduser()
    else:
        output_dir = None

    return image_path, output_dir


# --------------------------------------------------------------------
# Image loading helpers
# --------------------------------------------------------------------

def load_image_as_gray(image_path: Path) -> np.ndarray:
    """
    Load an image and return a grayscale uint8 image.

    Handles:
      - RAW (ARW, CR2, etc.) if rawpy is available.
      - Standard formats via OpenCV otherwise.
    """
    ext = image_path.suffix.lower()

    is_raw = ext in [".arw", ".cr2", ".nef", ".rw2", ".dng", ".orf", ".raf"]

    if is_raw:
        if not HAS_RAWPY:
            raise RuntimeError(
                f"rawpy is not installed, but a RAW file was provided: {image_path}\n"
                "Install it in your environment, e.g.:\n"
                "  pip install rawpy imageio\n"
            )
        with rawpy.imread(str(image_path)) as raw:
            rgb = raw.postprocess(
                use_camera_wb=True,
                no_auto_bright=True,
                output_bps=16,
            )  # shape (H, W, 3), uint16
        # Convert to float in [0,1], then to uint8
        rgb_f = (rgb.astype(np.float32) / 65535.0)
        gray_f = cv2.cvtColor(rgb_f, cv2.COLOR_RGB2GRAY)
        gray_u8 = np.clip(gray_f * 255.0, 0, 255).astype(np.uint8)
        return gray_u8
    else:
        img = cv2.imread(str(image_path), cv2.IMREAD_UNCHANGED)
        if img is None:
            raise FileNotFoundError(f"Could not read image: {image_path}")

        if img.ndim == 2:
            # already grayscale
            if img.dtype != np.uint8:
                # normalize to uint8
                img_f = img.astype(np.float32)
                img_f -= img_f.min()
                if img_f.max() > 0:
                    img_f /= img_f.max()
                img_u8 = (img_f * 255.0).astype(np.uint8)
                return img_u8
            return img
        else:
            # color image; convert to gray
            gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
            return gray


def load_mask(mask_path: Path, shape: Tuple[int, int]) -> np.ndarray:
    """
    Load the mask image and convert to a binary 0/1 mask, resized if needed.
    """
    if not mask_path.exists():
        raise FileNotFoundError(f"Mask file not found: {mask_path}")

    mask = cv2.imread(str(mask_path), cv2.IMREAD_GRAYSCALE)
    if mask is None:
        raise RuntimeError(f"Could not read mask image: {mask_path}")

    # Resize mask if dimensions don't match
    if mask.shape != shape:
        mask = cv2.resize(mask, (shape[1], shape[0]), interpolation=cv2.INTER_NEAREST)

    # Convert to binary: 1 = keep, 0 = discard
    _, mask_bin = cv2.threshold(mask, 127, 1, cv2.THRESH_BINARY)
    return mask_bin.astype(np.uint8)


# --------------------------------------------------------------------
# Stage operations
# --------------------------------------------------------------------

def apply_median_filter(gray: np.ndarray) -> np.ndarray:
    if MEDIAN_WINDOW <= 1:
        return gray.copy()
    k = MEDIAN_WINDOW
    if k % 2 == 0:
        k += 1  # ensure odd
    return cv2.medianBlur(gray, k)


def apply_threshold(gray: np.ndarray) -> np.ndarray:
    """
    Apply either adaptive or simple thresholding.
    Returns a binary uint8 image with values 0/255.
    """
    if ADAPTIVE_THRESHOLD_WINDOW > 0:
        k = ADAPTIVE_THRESHOLD_WINDOW
        if k % 2 == 0:
            k += 1
        if ADAPTIVE_THRESHOLD_TYPE.lower() == "gaussian":
            method = cv2.ADAPTIVE_THRESH_GAUSSIAN_C
        else:
            method = cv2.ADAPTIVE_THRESH_MEAN_C

        thresh = cv2.adaptiveThreshold(
            gray,
            maxValue=255,
            adaptiveMethod=method,
            thresholdType=cv2.THRESH_BINARY,
            blockSize=k,
            C=ADAPTIVE_THRESHOLD_BIAS,
        )
        return thresh
    elif SIMPLE_THRESHOLD != 0:
        _, thresh = cv2.threshold(gray, SIMPLE_THRESHOLD, 255, cv2.THRESH_BINARY)
        return thresh
    else:
        # No thresholding: return a copy
        return gray.copy()


def apply_mask(binary_img: np.ndarray, mask_bin: np.ndarray) -> np.ndarray:
    """
    Apply a binary 0/1 mask to a 0/255 image. Mask=1 retains, 0 kills.
    """
    masked = binary_img.copy()
    masked[mask_bin == 0] = 0
    return masked


def apply_dilation(binary_img: np.ndarray) -> np.ndarray:
    """
    Apply DILATION_COUNT iterations of dilation to a binary 0/255 image.
    """
    if DILATION_COUNT <= 0:
        return binary_img.copy()
    kernel = np.ones((3, 3), np.uint8)
    dilated = cv2.dilate(binary_img, kernel, iterations=DILATION_COUNT)
    return dilated


def detect_dots(binary_img: np.ndarray) -> Tuple[np.ndarray, list]:
    """
    Detect connected components (contours) in a binary 0/255 image and
    return:
      - A label image (for debugging, though we may not save it)
      - A list of dot contours that satisfy size constraints (area / radius)

    This is a simplified stand-in for the C++ dot-detection logic.
    """
    # OpenCV findContours expects 0/255
    contours, _ = cv2.findContours(
        binary_img, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
    )

    h, w = binary_img.shape
    label_img = np.zeros((h, w), dtype=np.int32)

    dots = []
    label_value = 1

    for cnt in contours:
        area = cv2.contourArea(cnt)
        if area <= 0:
            continue

        # approximate "radius" via equivalent circle
        radius = np.sqrt(area / np.pi)

        if area < MIN_DOT_AREA or area > MAX_DOT_AREA:
            continue
        if radius < MIN_DOT_RADIUS or radius > MAX_DOT_RADIUS:
            continue

        # Draw contour index into label image (just for debugging)
        cv2.drawContours(label_img, [cnt], -1, int(label_value), thickness=-1)
        label_value += 1
        dots.append(cnt)

    return label_img, dots


# --------------------------------------------------------------------
# Main driver
# --------------------------------------------------------------------

def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]

    # Parse arguments
    image_path, explicit_out_dir = parse_args(argv)

    if not image_path.exists():
        raise FileNotFoundError(f"Image file does not exist: {image_path}")

    # Determine output directory
    if explicit_out_dir is not None:
        out_dir = explicit_out_dir
    else:
        out_dir = image_path.parent / f"pltfilter_stages_{image_path.stem}"

    out_dir.mkdir(parents=True, exist_ok=True)

    print("\n=== PLTFilterStages ===")
    print(f"Input image:    {image_path}")
    print(f"Output directory: {out_dir}")
    print(f"rawpy available: {HAS_RAWPY}")

    # Stage 1: load and gray
    gray = load_image_as_gray(image_path)
    cv2.imwrite(str(out_dir / "stage_01_raw_gray.png"), gray)
    print("  Saved stage_01_raw_gray.png")

    # Stage 2: median filter
    median_img = apply_median_filter(gray)
    cv2.imwrite(str(out_dir / "stage_02_median.png"), median_img)
    print("  Saved stage_02_median.png")

    # Stage 3: threshold
    thresh_img = apply_threshold(median_img)
    cv2.imwrite(str(out_dir / "stage_03_threshold.png"), thresh_img)
    print("  Saved stage_03_threshold.png")

    # Stage 4: mask
    mask_path = Path(MASK_FILE).expanduser()
    try:
        mask_bin = load_mask(mask_path, shape=thresh_img.shape)
        masked_img = apply_mask(thresh_img, mask_bin)
        cv2.imwrite(str(out_dir / "stage_04_masked.png"), masked_img)
        print("  Saved stage_04_masked.png")
    except Exception as e:
        print(f"  WARNING: Could not apply mask ({e}); using threshold image as masked.")
        masked_img = thresh_img.copy()

    # Stage 5: dilation
    dilated_img = apply_dilation(masked_img)
    cv2.imwrite(str(out_dir / "stage_05_dilated.png"), dilated_img)
    print("  Saved stage_05_dilated.png")

    # Stage 6: detect dots & label image
    labels, dots = detect_dots(dilated_img)

    # For visualization, scale labels to 0-255
    if labels.max() > 0:
        labels_u8 = (255.0 * labels.astype(np.float32) / labels.max()).astype(np.uint8)
    else:
        labels_u8 = labels.astype(np.uint8)
    cv2.imwrite(str(out_dir / "stage_06_labels.png"), labels_u8)
    print("  Saved stage_06_labels.png")

    # Stage 7: overlay dots on original gray
    overlay = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)

    for cnt in dots:
        (x, y), radius = cv2.minEnclosingCircle(cnt)
        center = (int(x), int(y))
        r_int = max(1, int(radius))
        cv2.circle(overlay, center, r_int, (0, 0, 255), 1)

    cv2.imwrite(str(out_dir / "stage_07_dots_overlay.png"), overlay)
    print("  Saved stage_07_dots_overlay.png")
    print(f"  Detected {len(dots)} dots (after size filtering).")
    print("\nDone.\n")


if __name__ == "__main__":
    main()
