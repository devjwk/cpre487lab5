# Lab 5 Section 5.3: accuracy of the quantized model when each layer uses its own bit width.
#
# A numpy copy of the C++ quantized inference (sw/framework: computeQuantized), so that many per-layer
# bit-width choices can be tried quickly on the 1000 exported validation images.
#   acc = bias + sum((in - inZero) * w)   int MACs, int32 accumulate
#   y   = acc / (inScale * wScale)        dequantize, ReLU, then requantize with the next layer's parameters
# Usage: python3 util/mixed_precision_study.py            (from the repository root)
import json
import os
import sys
import numpy as np

DATA = "sw/framework/data"
F32 = np.float32
# name, kernel, in channels, out channels, max-pool after, Keras name of the activation it receives
LAYERS = [("conv1", 5, 3, 32, False, "input"), ("conv2", 5, 32, 32, True, "conv2d"),
          ("conv3", 3, 32, 64, False, "max_pooling2d"), ("conv4", 3, 64, 64, True, "conv2d_2"),
          ("conv5", 3, 64, 64, False, "max_pooling2d_1"), ("conv6", 3, 64, 128, True, "conv2d_4"),
          ("dense1", 0, 2048, 256, False, "flatten"), ("dense2", 0, 256, 200, False, "dense")]
MACS = [8640000, 80281600, 12460032, 21233664, 3686400, 4718592, 524288, 51200]  # per inference


def load_float():
    out = []
    for name, k, cin, cout, _, _ in LAYERS:
        w = np.fromfile(f"{DATA}/model/{name}_weights.bin", dtype=F32)
        b = np.fromfile(f"{DATA}/model/{name}_biases.bin", dtype=F32)
        out.append((w.reshape((k * k * cin, cout) if k else (cin, cout)), b))  # HWIO / (in, out): columns = outputs
    return out


def quantize_layer(w, b, bits, r):
    """Same rules as util/quantize_export.py. r = {"min", "max", "avg"} of the layer's input activation."""
    qmax = 2 ** (bits - 1) - 1
    s_i = qmax / max(abs(r["max"] - r["avg"]), abs(r["min"] - r["avg"]))
    z_i = -int(np.round(r["avg"] * s_i))
    s_w = qmax / np.abs(w).max()
    return dict(bits=bits, s_i=F32(s_i), z_i=z_i, s_w=F32(s_w),
                w=np.round(s_w * w).astype(np.int8).astype(np.float64), b=np.round(s_i * s_w * b).astype(np.int64))


def float_inputs(images_u8, floats, keep=400000, seed=0):
    """fp32 forward pass; returns a random sample of the input activation of every conv/dense layer."""
    rng = np.random.default_rng(seed)
    x = images_u8.astype(F32) / F32(255)
    samples = []
    for i, ((name, k, cin, cout, pool, _), (w, b)) in enumerate(zip(LAYERS, floats)):
        if name == "dense1":
            x = x.reshape(len(x), -1)
        samples.append(rng.choice(x.ravel(), size=min(keep, x.size), replace=False))
        y = (patches(x, k) if k else x) @ w + b
        if i < len(LAYERS) - 1:
            y = np.maximum(y, 0)
        if pool:
            n, h, w_, c = y.shape
            y = y.reshape(n, h // 2, 2, w_ // 2, 2, c).max(axis=(2, 4))
        x = y
    return samples


CLIPS = (100, 99.999, 99.99, 99.9, 99.7, 99.5, 99, 98.5, 98, 97, 96, 95, 93, 90)  # percentiles tried as the clip value


def calibrate_layer(w, b, bits, act):
    """Calibrated quantization. Every input activation is >= 0 (image or ReLU output), so the whole signed
    range is used for [0, clip] (zero point = most negative code). The clip value of the activation and of
    the weights is the percentile that gives the smallest squared error after quantization.
    The MAC still gets signed `bits`-bit operands; the zero point is corrected in software as before."""
    lo, hi = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    best = None
    for c in np.unique(np.percentile(act, CLIPS)):
        if c <= 0:
            continue
        s = (hi - lo) / c
        err = np.mean((np.clip(np.rint(act * s), 0, hi - lo) / s - act) ** 2)
        if best is None or err < best[0]:
            best = (err, s)
    s_i, z_i = best[1], lo
    best = None
    for c in np.unique(np.percentile(np.abs(w), CLIPS)):
        s = hi / c
        err = np.mean((np.clip(np.rint(w * s), lo, hi) / s - w) ** 2)
        if best is None or err < best[0]:
            best = (err, s)
    s_w = best[1]
    return dict(bits=bits, s_i=F32(s_i), z_i=z_i, s_w=F32(s_w),
                w=np.clip(np.rint(s_w * w), lo, hi).astype(np.float64), b=np.round(s_i * s_w * b).astype(np.int64))


def quant(x, scale, zero, bits):  # Quant.h quantize(): round to nearest even, clamp to the signed range
    q = np.rint(x.astype(F32) * scale) + zero
    return np.clip(q, -(1 << (bits - 1)), (1 << (bits - 1)) - 1)


def patches(x, k):  # (N, H, W, C) -> (N, H-k+1, W-k+1, k*k*C) in (ky, kx, ic) order
    v = np.lib.stride_tricks.sliding_window_view(x, (k, k), axis=(1, 2))  # (N, H', W', C, ky, kx)
    return v.transpose(0, 1, 2, 4, 5, 3).reshape(*v.shape[:3], -1)


def infer(images_u8, q):
    """images_u8: (N, 64, 64, 3) uint8. q: one quantize_layer() result per conv/dense layer. Returns logits."""
    x = quant(images_u8.astype(F32) / F32(255), q[0]["s_i"], q[0]["z_i"], q[0]["bits"])
    for i, (name, k, cin, cout, pool, _) in enumerate(LAYERS):
        p = q[i]
        if name == "dense1":
            x = x.reshape(len(x), -1)
        cols = patches(x, k) if k else x
        acc = (cols - p["z_i"]) @ p["w"] + p["b"]                 # exact: integers in float64
        y = acc.astype(F32) / (p["s_i"] * p["s_w"])
        if i == len(LAYERS) - 1:
            return y                                              # fp32 logits (softmax keeps the order)
        y = np.maximum(y, 0)
        if pool:
            n, h, w_, c = y.shape
            y = y.reshape(n, h // 2, 2, w_ // 2, 2, c).max(axis=(2, 4))  # max before/after requantize is the same
        x = quant(y, q[i + 1]["s_i"], q[i + 1]["z_i"], q[i + 1]["bits"])


def accuracy(logits, labels):  # top-1 / top-10 in %, ties go to the lower class index like the C++ check
    true = logits[np.arange(len(labels)), labels][:, None]
    rank = (logits > true).sum(1) + ((logits == true) & (np.arange(logits.shape[1]) < labels[:, None])).sum(1)
    return 100 * (rank == 0).mean(), 100 * (rank < 10).mean()


def evaluate(bits, floats, ranges, images, labels, batch=50):
    q = [quantize_layer(w, b, n, ranges[L[5]]) for (w, b), n, L in zip(floats, bits, LAYERS)]
    logits = np.concatenate([infer(images[i:i + batch], q) for i in range(0, len(images), batch)])
    return accuracy(logits, labels)


def avg_bits(bits):  # MAC-weighted average operand width
    return sum(b * m for b, m in zip(bits, MACS)) / sum(MACS)


if __name__ == "__main__":
    images = np.fromfile(f"{DATA}/val/val_images_u8.bin", dtype=np.uint8).reshape(-1, 64, 64, 3)
    labels = np.fromfile(f"{DATA}/val/val_labels_i32.bin", dtype=np.int32)
    floats = load_float()
    ranges = json.load(open(os.path.join(os.path.dirname(__file__), "activation_ranges.json")))
    # The last 200 images are only used to calibrate; every accuracy below is on the first 800.
    CAL, EV = slice(800, 1000), slice(0, 800)
    acts = float_inputs(images[CAL], floats)
    cache = {}

    def layer(i, bits, scheme):
        if (i, bits, scheme) not in cache:
            w, b = floats[i]
            cache[i, bits, scheme] = (quantize_layer(w, b, bits, ranges[LAYERS[i][5]]) if scheme == "lab4"
                                      else calibrate_layer(w, b, bits, acts[i]))
        return cache[i, bits, scheme]

    def run(bits, scheme, sel=EV):
        q = [layer(i, b, scheme) for i, b in enumerate(bits)]
        logits = np.concatenate([infer(images[sel][j:j + 50], q) for j in range(0, len(images[sel]), 50)])
        return accuracy(logits, labels[sel])

    # `export b1 ... b8`: write the calibrated model with these per-layer widths for the C++ framework
    # (data/model/qvar: int8 weights, int32 biases, quant_params.txt with a bits column) and stop.
    if len(sys.argv) > 1 and sys.argv[1] == "export":
        bits = [int(b) for b in sys.argv[2:10]]
        out = f"{DATA}/model/qvar"
        os.makedirs(out, exist_ok=True)
        with open(f"{out}/quant_params.txt", "w") as params:
            for i, (L, n) in enumerate(zip(LAYERS, bits)):
                p = layer(i, n, "calibrated")
                shape = floats[i][0].shape
                p["w"].reshape(shape).astype(np.int8).tofile(f"{out}/{L[0]}_weights.bin")
                p["b"].astype(np.int32).tofile(f"{out}/{L[0]}_biases.bin")
                params.write(f"{L[0]} {p['s_i']:.9g} {p['z_i']} {p['s_w']:.9g} {n}\n")
        print(f"wrote {out}, bits {bits}: top-1 %.1f top-10 %.1f on the 800 images," % run(bits, "calibrated"),
              "%.1f / %.1f on all 1000" % run(bits, "calibrated", slice(0, 1000)))
        sys.exit()

    for scheme in ("lab4", "calibrated"):
        print(f"=== {scheme} quantization, 800 images: top-1 / top-10 ===")
        for b in (8, 4, 2):
            print(f"  whole model {b} bit: %5.1f /%5.1f" % run([b] * 8, scheme))
        for i, L in enumerate(LAYERS):
            row = [("%5.1f /%5.1f" % run([8] * i + [b] + [8] * (7 - i), scheme)) for b in (4, 2)]
            print(f"  only {L[0]:7s} lowered ({100 * MACS[i] / sum(MACS):4.1f}% of MACs)   4 bit {row[0]}    2 bit {row[1]}")

    # Greedy search for the per-layer widths (calibrated scheme). One 32-bit FIFO word holds 32 / (2 * bits)
    # weight/activation pairs, so the cost of a layer is MACs * bits / 16 words. Starting from 8 bit everywhere,
    # lower the layer that saves the most words while top-1 and top-10 stay within `tol` points of the 8-bit model.
    words = lambda bits: sum(m * b / 16 for m, b in zip(MACS, bits))
    seen = {}
    score = lambda bits: seen.setdefault(tuple(bits), run(bits, "calibrated"))
    base = score([8] * 8)
    for tol in (1.0, 2.0, 3.0):
        bits = [8] * 8
        while True:
            options = []
            for i in range(8):
                if bits[i] > 2:
                    trial = bits[:i] + [bits[i] // 2] + bits[i + 1:]
                    t1, t10 = score(trial)
                    if base[0] - t1 <= tol and base[1] - t10 <= tol:
                        options.append((words(bits) - words(trial), trial))
            if not options:
                break
            bits = max(options)[1]
        print(f"tolerance {tol} points: bits {bits}  top-1 %.1f top-10 %.1f" % score(bits),
              " FIFO words vs all 8 bit: %.1f%%" % (100 * words(bits) / words([8] * 8)), " average bits %.2f" % avg_bits(bits))
