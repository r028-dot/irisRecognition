"""
demo_server.py
==============
Python demonstration of the SERVER-SIDE iris recognition pipeline.
Mirrors the C++ implementation exactly:
  1. Segmentation     (Hough Circles  -- same as segmentIris() in Normalization.cpp)
  2. Normalization    (Rubber Sheet   -- same as Normalization::normalize())
  3. Feature Extract  (Log-Gabor bank -- same as FeatureExtractor::extract())
  4. IrisCode         (2048-bit array -- same as IrisCode struct)
  5. Hamming Distance (masked HD      -- same as IrisCode::hammingDistance())
  6. Match decision   (threshold 0.32 -- same as IrisProcessor MATCH_THRESHOLD)

Usage:
    python demo_server.py              # uses existing demo_output/step6_iris_crop.jpg
    python demo_server.py <eye.jpg>    # uses a custom eye image
"""

import cv2
import numpy as np
import os
import sys

OUTPUT_DIR = "demo_output"
os.makedirs(OUTPUT_DIR, exist_ok=True)

NORM_W  = 512   # angular resolution  (same as config.json normalizedWidth)
NORM_H  = 64    # radial resolution   (same as config.json normalizedHeight)
HD_THRESHOLD = 0.32    # Hamming distance threshold (same as MATCH_THRESHOLD)
MIN_VALID_BITS = 1000  # minimum unmasked bits     (same as MIN_VALID_BITS)

# ──────────────────────────────────────────────────────────────────────────────
# helpers
# ──────────────────────────────────────────────────────────────────────────────

def save_img(tag, img, step, title):
    out = img.copy()
    if len(out.shape) == 2:
        out = cv2.cvtColor(out, cv2.COLOR_GRAY2BGR)
    min_w = 400
    if out.shape[1] < min_w:
        s = min_w / out.shape[1]
        out = cv2.resize(out, (int(out.shape[1]*s), int(out.shape[0]*s)),
                         interpolation=cv2.INTER_NEAREST)
    max_w = 900
    if out.shape[1] > max_w:
        s = max_w / out.shape[1]
        out = cv2.resize(out, (int(out.shape[1]*s), int(out.shape[0]*s)))
    cv2.rectangle(out, (0, out.shape[0]-36), (out.shape[1], out.shape[0]), (20,20,20), -1)
    cv2.putText(out, f"[Server] Step {step}: {title}",
                (6, out.shape[0]-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255,255,255), 1)
    path = os.path.join(OUTPUT_DIR, f"srv_step{step}_{tag}.jpg")
    cv2.imwrite(path, out, [cv2.IMWRITE_JPEG_QUALITY, 95])
    print(f"  saved -> {path}")
    return path


def make_synthetic_eye(size=300):
    """
    Generate a synthetic grayscale eye image so the demo works even without
    a real photo.  Creates:
      - dark circular pupil
      - radial iris texture (random lines + noise)
      - bright sclera background
      - two horizontal eyelid strips
    """
    img = np.full((size, size), 230, dtype=np.uint8)   # sclera
    cx, cy = size // 2, size // 2
    r_iris  = size // 3
    r_pupil = size // 8

    # iris base (medium grey)
    cv2.circle(img, (cx, cy), r_iris, 130, -1)

    # radial iris texture – random lines from centre
    rng = np.random.default_rng(42)
    for _ in range(180):
        angle  = rng.uniform(0, 2 * np.pi)
        r_from = r_pupil + 2
        r_to   = r_iris  - 2
        x1 = int(cx + r_from * np.cos(angle))
        y1 = int(cy + r_from * np.sin(angle))
        x2 = int(cx + r_to   * np.cos(angle))
        y2 = int(cy + r_to   * np.sin(angle))
        col = int(rng.integers(60, 170))
        cv2.line(img, (x1, y1), (x2, y2), col, 1)

    # add some circular furrows
    for rr in [r_pupil + int((r_iris-r_pupil)*f) for f in (0.35, 0.65)]:
        for a in np.linspace(0, 2*np.pi, 200):
            px = int(cx + rr * np.cos(a))
            py = int(cy + rr * np.sin(a))
            if 0 <= px < size and 0 <= py < size:
                img[py, px] = max(0, int(img[py, px]) - 40)

    # noise
    noise = rng.integers(-12, 13, img.shape, dtype=np.int16)
    img   = np.clip(img.astype(np.int16) + noise, 0, 255).astype(np.uint8)

    # smooth slightly
    img = cv2.GaussianBlur(img, (3, 3), 0.8)

    # pupil
    cv2.circle(img, (cx, cy), r_pupil, 15, -1)

    # specular highlight (bright spot on pupil)
    hx = cx + r_pupil // 3
    hy = cy - r_pupil // 3
    cv2.circle(img, (hx, hy), max(2, r_pupil // 5), 240, -1)

    # eyelids (dark strips top/bottom)
    eyelid_h = size // 9
    cv2.ellipse(img, (cx, cy - r_iris + eyelid_h // 2),
                (r_iris + 10, eyelid_h), 0, 180, 360, 40, -1)
    cv2.ellipse(img, (cx, cy + r_iris - eyelid_h // 2),
                (r_iris + 10, eyelid_h), 0, 0,   180, 40, -1)

    return img


# ──────────────────────────────────────────────────────────────────────────────
# Step 1: Segmentation  (mirrors segmentIris() in Normalization.cpp)
# ──────────────────────────────────────────────────────────────────────────────

def segment_iris(gray):
    H, W  = gray.shape
    blurred = cv2.GaussianBlur(gray, (7, 7), 2.0)

    # ── pupil ── try Hough, fall back to darkest-region estimate
    pupil_circles = cv2.HoughCircles(
        blurred, cv2.HOUGH_GRADIENT, dp=1.5,
        minDist=H // 8,
        param1=80, param2=25,
        minRadius=max(6, W // 12), maxRadius=W // 4)

    if pupil_circles is not None:
        best_pupil = None
        best_mean  = 1e9
        for c in pupil_circles[0]:
            mask = np.zeros(gray.shape, np.uint8)
            cv2.circle(mask, (int(c[0]), int(c[1])), int(c[2]), 255, -1)
            m = cv2.mean(gray, mask)[0]
            if m < best_mean:
                best_mean  = m
                best_pupil = c
        px, py, pr = int(best_pupil[0]), int(best_pupil[1]), int(best_pupil[2])
    else:
        # Fallback: locate darkest compact region via minMaxLoc on blurred image
        eroded  = cv2.erode(blurred, np.ones((5, 5), np.uint8), iterations=3)
        _, _, min_loc, _ = cv2.minMaxLoc(eroded)
        px, py  = min_loc
        pr      = max(8, W // 10)

    # ── iris ── Hough on a wider range; fall back to size-based estimate
    min_iris = max(int(pr * 1.8), W // 5)
    max_iris = min(W, H) // 2

    iris_circles = cv2.HoughCircles(
        blurred, cv2.HOUGH_GRADIENT, dp=1.5,
        minDist=H // 4,
        param1=60, param2=25,
        minRadius=min_iris, maxRadius=max_iris)

    if iris_circles is not None:
        best_iris = min(iris_circles[0],
                        key=lambda c: (c[0]-px)**2 + (c[1]-py)**2)
        ix, iy, ir = int(best_iris[0]), int(best_iris[1]), int(best_iris[2])
    else:
        # Fallback: image centre, iris fills ~90% of the shorter dimension
        ix, iy = W // 2, H // 2
        ir     = int(min(W, H) * 0.45)

    return dict(px=px, py=py, pr=pr, ix=ix, iy=iy, ir=ir)


# ──────────────────────────────────────────────────────────────────────────────
# Step 2: Normalization  (Daugman Rubber Sheet – mirrors Normalization::normalize())
# ──────────────────────────────────────────────────────────────────────────────

def rubber_sheet(gray, region, width=NORM_W, height=NORM_H):
    normalized = np.zeros((height, width), dtype=np.uint8)
    occ_mask   = np.ones((height, width), dtype=np.uint8) * 255

    px, py, pr = region['px'], region['py'], region['pr']
    ir = region['ir']

    for row in range(height):
        r_norm = (row + 0.5) / height
        R      = pr * (1.0 - r_norm) + ir * r_norm

        for col in range(width):
            theta = 2 * np.pi * col / width
            sx = int(round(px + R * np.cos(theta)))
            sy = int(round(py + R * np.sin(theta)))

            if 0 <= sx < gray.shape[1] and 0 <= sy < gray.shape[0]:
                normalized[row, col] = gray[sy, sx]
            else:
                occ_mask[row, col] = 0

    # mask bright specular reflections
    _, bright = cv2.threshold(normalized, 240, 255, cv2.THRESH_BINARY)
    occ_mask  = cv2.bitwise_and(occ_mask, cv2.bitwise_not(bright))

    return normalized, occ_mask


# ──────────────────────────────────────────────────────────────────────────────
# Step 3: Log-Gabor feature extraction  (mirrors FeatureExtractor in C++)
# Returns IrisCode as two uint8 arrays: bits[256], mask[256]  (2048 bits total)
# ──────────────────────────────────────────────────────────────────────────────

def log_gabor_1d(signal_f32, center_freq, bandwidth):
    """Apply 1D Log-Gabor filter in frequency domain, return complex response."""
    N   = len(signal_f32)
    sig = np.zeros(2 * N, dtype=np.float32)
    sig[:N] = signal_f32

    S   = np.fft.fft(sig)
    log_bw2 = np.log(bandwidth) ** 2

    H = np.zeros(2 * N, dtype=np.float32)
    for k in range(1, 2 * N):
        f = k / (2 * N)
        if f < 1e-9:
            continue
        lr = np.log(f / center_freq)
        H[k] = np.exp(-(lr * lr) / (2 * log_bw2))

    resp_f  = S * H
    resp_t  = np.fft.ifft(resp_f)
    return resp_t[:N]   # complex, first N samples


def extract_iris_code(normalized, occ_mask):
    """
    Extract 2048-bit IrisCode using 4 Log-Gabor frequency bands
    applied across 8 horizontal strip-bands.
    Matches FeatureExtractor::extract() logic in C++.
    """
    num_bands  = 8
    num_freqs  = 4
    freqs      = [0.1, 0.2, 0.3, 0.4]
    bandwidth  = 0.5

    H, W = normalized.shape
    band_h    = H // num_bands
    bits_byte  = np.zeros(256, dtype=np.uint8)
    mask_byte  = np.zeros(256, dtype=np.uint8)

    bit_idx = 0
    for band in range(num_bands):
        strip = normalized[band*band_h:(band+1)*band_h, :]
        mstrip = occ_mask[band*band_h:(band+1)*band_h, :]

        row_mean  = strip.mean(axis=0).astype(np.float32)
        mask_mean = mstrip.mean(axis=0).astype(np.float32)

        for f in freqs:
            resp = log_gabor_1d(row_mean, f, bandwidth)

            bits_per_band = W // 8   # 64 bits per band/freq
            for i in range(bits_per_band):
                sample_idx = i * 8
                if sample_idx >= W:
                    break

                real_part = resp[sample_idx].real
                mask_val  = mask_mean[sample_idx]

                byte_idx = bit_idx // 8
                bit_off  = 7 - (bit_idx % 8)

                if byte_idx < 256:
                    if real_part >= 0:
                        bits_byte[byte_idx] |= (1 << bit_off)
                    if mask_val > 127:
                        mask_byte[byte_idx] |= (1 << bit_off)

                bit_idx += 1

    return bits_byte, mask_byte


def hamming_distance(bits1, mask1, bits2, mask2):
    """Masked Hamming Distance – mirrors IrisCode::hammingDistance()."""
    diff_bits  = 0
    valid_bits = 0
    for i in range(256):
        m = int(mask1[i]) & int(mask2[i])
        diff_bits  += bin((int(bits1[i]) ^ int(bits2[i])) & m).count('1')
        valid_bits += bin(m).count('1')
    return diff_bits / valid_bits if valid_bits > 0 else 1.0, valid_bits


# ──────────────────────────────────────────────────────────────────────────────
# Visualisation helpers
# ──────────────────────────────────────────────────────────────────────────────

def visualise_segmentation(gray, region):
    vis = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
    cv2.circle(vis, (region['ix'], region['iy']), region['ir'], (0, 165, 255), 2)
    cv2.circle(vis, (region['px'], region['py']), region['pr'], (0,   0, 255), 2)
    cv2.circle(vis, (region['px'], region['py']), 2,            (255, 255, 0), -1)
    cv2.putText(vis, f"Iris  r={region['ir']}", (4, 18),
                cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0,165,255), 1)
    cv2.putText(vis, f"Pupil r={region['pr']}", (4, 36),
                cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0,0,255),   1)
    return vis


def visualise_iris_code(bits, mask, width=512, height=48):
    """Draw the 2048-bit IrisCode as a black/white strip (gray = invalid)."""
    vis = np.zeros((height, width, 3), dtype=np.uint8)
    for bit_idx in range(2048):
        col     = (bit_idx * width) // 2048
        byte_i  = bit_idx // 8
        bit_off = 7 - (bit_idx % 8)

        valid = bool(mask[byte_i] & (1 << bit_off))
        bit   = bool(bits[byte_i] & (1 << bit_off))

        if not valid:
            color = (100, 100, 100)   # grey = occluded
        elif bit:
            color = (255, 255, 255)   # white = 1
        else:
            color = (0, 0, 0)         # black = 0

        vis[:, col] = color
    return vis


def gabor_response_heatmap(normalized):
    """Visualise the complex magnitude of the Gabor response for the first band."""
    row_mean = normalized[:8, :].mean(axis=0).astype(np.float32)
    resp = log_gabor_1d(row_mean, 0.2, 0.5)
    mag  = np.abs(resp).astype(np.float32)
    if mag.max() > 0:
        mag = (mag / mag.max() * 255).astype(np.uint8)
    heatmap = cv2.applyColorMap(
        cv2.resize(mag.reshape(1, -1), (512, 48), interpolation=cv2.INTER_NEAREST),
        cv2.COLORMAP_JET)
    return heatmap


# ──────────────────────────────────────────────────────────────────────────────
# Main pipeline
# ──────────────────────────────────────────────────────────────────────────────

def run_server_pipeline(eye_image_gray, label="A"):
    print(f"\n{'='*60}")
    print(f"  SERVER PIPELINE  –  sample '{label}'")
    print(f"{'='*60}")

    # ── Step S1: Receive image ─────────────────────────────
    print("\n[S1] Image received by server")
    save_img(f"received_{label}", eye_image_gray, "S1", f"Image received (sample {label})")

    # ── Step S2: CLAHE contrast enhancement ───────────────
    clahe   = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
    enhanced = clahe.apply(eye_image_gray)
    save_img(f"clahe_{label}", enhanced, "S2", f"CLAHE enhancement (sample {label})")
    print("[S2] CLAHE contrast enhancement applied")

    # ── Step S3: Segmentation ──────────────────────────────
    region = segment_iris(enhanced)
    if region is None:
        print("  ERROR: segmentation failed")
        return None
    print(f"[S3] Segmentation: iris r={region['ir']}  pupil r={region['pr']}")
    seg_vis = visualise_segmentation(enhanced, region)
    save_img(f"segmentation_{label}", seg_vis, "S3",
             f"Hough segmentation – iris=orange pupil=red (sample {label})")

    # ── Step S4: Rubber Sheet Normalisation ───────────────
    normalized, occ_mask = rubber_sheet(enhanced, region)
    print(f"[S4] Rubber Sheet normalisation -> {NORM_W}x{NORM_H} strip")
    # save normalized strip
    norm_vis = np.vstack([
        normalized,
        (occ_mask // 255 * 200).astype(np.uint8)   # mask below strip
    ])
    save_img(f"normalized_{label}", norm_vis, "S4",
             f"Rubber Sheet normalized strip + mask (sample {label})")

    # ── Step S5: Log-Gabor heatmap ─────────────────────────
    heatmap = gabor_response_heatmap(normalized)
    save_img(f"gabor_{label}", heatmap, "S5",
             f"Log-Gabor response magnitude (sample {label})")
    print("[S5] Log-Gabor filter applied")

    # ── Step S6: IrisCode extraction ───────────────────────
    bits, mask = extract_iris_code(normalized, occ_mask)
    valid_bits = sum(bin(int(b)).count('1') for b in mask)
    print(f"[S6] IrisCode extracted: {valid_bits} valid bits / 2048")
    code_vis = visualise_iris_code(bits, mask)
    save_img(f"iriscode_{label}", code_vis, "S6",
             f"IrisCode 2048-bit (white=1, black=0, grey=occluded) (sample {label})")

    if valid_bits < MIN_VALID_BITS:
        print(f"  WARNING: quality too low ({valid_bits} < {MIN_VALID_BITS})")

    return bits, mask, valid_bits


# ──────────────────────────────────────────────────────────────────────────────
# Entry point
# ──────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    # ── Load or generate eye image ────────────────────────
    existing_crop = os.path.join(OUTPUT_DIR, "step6_iris_crop.jpg")

    if len(sys.argv) > 1:
        src_path = sys.argv[1]
        eye_bgr  = cv2.imread(src_path)
        if eye_bgr is None:
            print(f"Error: cannot read {src_path}")
            sys.exit(1)
        eye_gray = cv2.cvtColor(eye_bgr, cv2.COLOR_BGR2GRAY)
        print(f"Loaded image: {src_path}")
    elif os.path.exists(existing_crop):
        eye_bgr  = cv2.imread(existing_crop)
        eye_gray = cv2.cvtColor(eye_bgr, cv2.COLOR_BGR2GRAY)
        print(f"Using existing iris crop: {existing_crop}")
    else:
        print("No image found – generating synthetic eye")
        eye_gray = make_synthetic_eye(300)
        cv2.imwrite(os.path.join(OUTPUT_DIR, "synthetic_eye.jpg"), eye_gray)

    # ── Run pipeline for Sample A (enrollment) ────────────
    result_A = run_server_pipeline(eye_gray, label="A")

    # ── Create a slightly different version (simulate re-scan) ──
    # Add small Gaussian noise only (no rotation) - realistic second scan
    rng   = np.random.default_rng(7)
    noise = rng.integers(-6, 7, eye_gray.shape, dtype=np.int16)
    eye_B = np.clip(eye_gray.astype(np.int16) + noise, 0, 255).astype(np.uint8)
    # slight brightness shift (+5) to simulate different lighting
    eye_B = np.clip(eye_B.astype(np.int16) + 5, 0, 255).astype(np.uint8)

    # ── Run pipeline for Sample B (verification probe) ────
    result_B = run_server_pipeline(eye_B, label="B")

    # ── Create an UNRELATED eye (should NOT match) ────────
    # Use a different random seed so the synthetic iris texture is completely different
    eye_C = make_synthetic_eye(max(eye_gray.shape[0], eye_gray.shape[1]))
    eye_C = cv2.resize(eye_C, (eye_gray.shape[1], eye_gray.shape[0]))
    result_C = run_server_pipeline(eye_C, label="C_different_person")

    # ── Comparison ────────────────────────────────────────
    print(f"\n{'='*60}")
    print("  MATCHING RESULTS  (mirrors IrisMatcher::compare)")
    print(f"{'='*60}")

    if result_A and result_B:
        bA, mA, vA = result_A
        bB, mB, vB = result_B
        hd_same, valid_same = hamming_distance(bA, mA, bB, mB)
        match_same = "MATCH" if hd_same <= HD_THRESHOLD else "NO MATCH"
        print(f"\n  Sample A vs B  (SAME person, different scan):")
        print(f"    Hamming Distance = {hd_same:.4f}  (threshold = {HD_THRESHOLD})")
        print(f"    Valid bits used  = {valid_same}")
        print(f"    Decision         = >>> {match_same} <<<")

    if result_A and result_C:
        bA, mA, vA = result_A
        bC, mC, vC = result_C
        hd_diff, valid_diff = hamming_distance(bA, mA, bC, mC)
        match_diff = "MATCH" if hd_diff <= HD_THRESHOLD else "NO MATCH"
        print(f"\n  Sample A vs C  (DIFFERENT person):")
        print(f"    Hamming Distance = {hd_diff:.4f}  (threshold = {HD_THRESHOLD})")
        print(f"    Valid bits used  = {valid_diff}")
        print(f"    Decision         = >>> {match_diff} <<<")

    # ── Final comparison visual ────────────────────────────
    if result_A and result_B and result_C:
        bA, mA, _ = result_A
        bB, mB, _ = result_B
        bC, mC, _ = result_C

        cA = visualise_iris_code(bA, mA)
        cB = visualise_iris_code(bB, mB)
        cC = visualise_iris_code(bC, mC)

        def labelled(img, text):
            h, w = img.shape[:2]
            canvas = np.zeros((h + 22, w, 3), np.uint8)
            canvas[:h] = img
            cv2.putText(canvas, text, (4, h + 16),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 200, 200), 1)
            return canvas

        cA = labelled(cA, f"A (enrolled)")
        cB = labelled(cB, f"B (same person) HD={hd_same:.3f}  -> {match_same}")
        cC = labelled(cC, f"C (diff person) HD={hd_diff:.3f}  -> {match_diff}")

        comparison = np.vstack([cA, cB, cC])
        save_img("comparison", comparison, "S7",
                 "IrisCode comparison: A=enrolled, B=same person, C=different")

    print(f"\nAll output images saved to: {OUTPUT_DIR}/")
    print("Look for files starting with 'srv_' for server-side steps.\n")
