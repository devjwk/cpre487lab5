# Lab 4 Section 4: export quantized weights/biases + per-layer scales for the C++ framework.
# Usage: python3 quantize_export.py <bits> [model_dir] [activation_ranges.json]
#   reads  <model_dir>/<layer>_weights.bin, <layer>_biases.bin (fp32)
#   writes <model_dir>/q<bits>/<layer>_weights.bin (int8), <layer>_biases.bin (int32), quant_params.txt
import json
import os
import sys
import numpy as np

bits = int(sys.argv[1]) if len(sys.argv) > 1 else 8
model_dir = sys.argv[2] if len(sys.argv) > 2 else "../framework/data/model"
ranges_file = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(__file__), "activation_ranges.json")

qmax = 2 ** (bits - 1) - 1  # 127 for 8 bit
ranges = json.load(open(ranges_file))

# C++ layer name -> Keras name of the activation it receives as input (from Section 3.3 profiling)
layer_inputs = [("conv1", "input"), ("conv2", "conv2d"), ("conv3", "max_pooling2d"), ("conv4", "conv2d_2"),
                ("conv5", "max_pooling2d_1"), ("conv6", "conv2d_4"), ("dense1", "flatten"), ("dense2", "dense")]

out_dir = os.path.join(model_dir, f"q{bits}")
os.makedirs(out_dir, exist_ok=True)

with open(os.path.join(out_dir, "quant_params.txt"), "w") as params:
    for name, src in layer_inputs:
        w = np.fromfile(os.path.join(model_dir, f"{name}_weights.bin"), dtype=np.float32)
        b = np.fromfile(os.path.join(model_dir, f"{name}_biases.bin"), dtype=np.float32)
        r = ranges[src]

        s_i = qmax / max(abs(r["max"] - r["avg"]), abs(r["min"] - r["avg"]))  # Si = 127 / max|Ix - avg(Ix)|
        z_i = -int(np.round(r["avg"] * s_i))                                   # zi = -round(avg(Ix) * Si)
        s_w = qmax / np.abs(w).max()                                           # Sw = 127 / max|Wx|
        s_b = s_i * s_w                                                        # Sb = Si * Sw

        np.round(s_w * w).astype(np.int8).tofile(os.path.join(out_dir, f"{name}_weights.bin"))
        np.round(s_b * b).astype(np.int32).tofile(os.path.join(out_dir, f"{name}_biases.bin"))
        params.write(f"{name} {s_i:.9g} {z_i} {s_w:.9g}\n")
        print(f"{name:7s} Si={s_i:9.4f} zi={z_i:4d} Sw={s_w:9.4f} Sb={s_b:11.4f}")

print(f"wrote {out_dir}")
