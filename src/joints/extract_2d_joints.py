"""
Simple 3DBiCar 2D keypoint extraction (replaces the notebook readjson / polygon2seg /
reshape_image_and_anno / show_* code and the Dataset class).

Reads   <folder>/image/raw_image.jpeg  and  <folder>/image/annotation.json
Writes  into --out (default: ./out):
    masked.png       512x512 cropped/padded image, background removed with the polygon mask
    keypoints.txt    "index x y status" in the 512x512 frame
                     status: -1 = does not exist, 0 = visible, 1 = hidden/covered
    masked_kps.png   masked.png with the keypoints drawn (blue = visible, green = hidden)

Usage:
    python extract_kps.py ./3DBiCar/<instance> [--out out] [--size 512]

Needs only: numpy, opencv-python, pillow.
"""
import argparse
import json
import math
import os

import cv2
import numpy as np
from PIL import Image, ImageDraw

NUM_KPS = 24


def read_annotation(json_path):
    """Returns polygon (N,2) and keypoints (24,3) = x, y, status in raw-image pixels."""
    with open(json_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    polygon = None
    kps = -np.ones((NUM_KPS, 3))
    for s in data["shapes"]:
        if s["shape_type"] == "polygon":
            polygon = np.array(s["points"], dtype=np.float64)
        elif s["shape_type"] == "point":
            _, index, hidden = s["label"].split("-")
            index = int(index.split("_")[-1])
            hidden = int(hidden[-1] == "1")
            kps[index] = [s["points"][0][0], s["points"][0][1], hidden]
    if polygon is None:
        raise ValueError("no polygon found in annotation")
    return polygon, kps


def process(folder, out_dir, size=512):
    img = np.array(Image.open(os.path.join(folder, "image", "raw_image.jpeg")).convert("RGB"))
    polygon, kps = read_annotation(os.path.join(folder, "image", "annotation.json"))
    H, W = img.shape[:2]

    # polygon -> binary mask (0/1)
    m = Image.new("L", (W, H), 0)
    ImageDraw.Draw(m).polygon([tuple(p) for p in polygon], outline=0, fill=1)
    mask = np.array(m, dtype=np.uint8)

    # crop to the polygon bounding box
    x1, y1 = np.floor(polygon.min(axis=0)).astype(int)
    x2, y2 = np.ceil(polygon.max(axis=0)).astype(int)
    x1, y1, x2, y2 = max(x1, 0), max(y1, 0), min(x2, W), min(y2, H)
    img, mask = img[y1:y2, x1:x2], mask[y1:y2, x1:x2]
    valid = kps[:, 2] != -1
    kps[valid, :2] -= [x1, y1]

    # pad to a square (image: white, mask: 0)
    h, w = mask.shape
    dif = abs(h - w)
    a, b = dif // 2, dif - dif // 2
    if h > w:   # pad left/right
        img = cv2.copyMakeBorder(img, 0, 0, a, b, cv2.BORDER_CONSTANT, value=(255, 255, 255))
        mask = cv2.copyMakeBorder(mask, 0, 0, a, b, cv2.BORDER_CONSTANT, value=0)
        kps[valid, 0] += a
    else:       # pad top/bottom
        img = cv2.copyMakeBorder(img, a, b, 0, 0, cv2.BORDER_CONSTANT, value=(255, 255, 255))
        mask = cv2.copyMakeBorder(mask, a, b, 0, 0, cv2.BORDER_CONSTANT, value=0)
        kps[valid, 1] += a

    # resize to size x size
    ratio = size / max(h, w)
    img = cv2.resize(img, (size, size), interpolation=cv2.INTER_AREA)
    mask = cv2.resize(mask, (size, size), interpolation=cv2.INTER_NEAREST)
    kps[valid, :2] *= ratio

    masked = img * mask[:, :, None]

    # draw keypoints on a copy of the masked image
    drawn = masked.copy()
    for i, (x, y, s) in enumerate(kps):
        if s == -1:
            continue
        color = (0, 0, 255) if s == 0 else (0, 255, 0)   # RGB: blue visible, green hidden
        cv2.circle(drawn, (int(round(x)), int(round(y))), 2, color, thickness=4)
        cv2.putText(drawn, str(i), (int(x) + 5, int(y) - 5),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.4, (255, 255, 255), 1, cv2.LINE_AA)

    os.makedirs(out_dir, exist_ok=True)
    Image.fromarray(masked).save(os.path.join(out_dir, "masked.png"))
    Image.fromarray(drawn).save(os.path.join(out_dir, "masked_kps.png"))
    with open(os.path.join(out_dir, "keypoints.txt"), "w") as f:
        f.write("# index x y status (-1 missing, 0 visible, 1 hidden)\n")
        for i, (x, y, s) in enumerate(kps):
            f.write(f"{i} {x:.3f} {y:.3f} {int(s)}\n")
    return masked, kps


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("folder", help="3DBiCar instance folder")
    ap.add_argument("--out", default="out")
    ap.add_argument("--size", type=int, default=512)
    args = ap.parse_args()
    _, kps = process(args.folder, args.out, args.size)
    print(f"Saved masked.png, keypoints.txt, masked_kps.png in {args.out}/")
