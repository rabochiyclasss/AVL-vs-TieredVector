import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")            # no display needed; render straight to file
import matplotlib.pyplot as plt

OUT = "plots"
os.makedirs(OUT, exist_ok=True)

df = pd.read_csv("results.csv")

# Friendly titles for each workload key.
TITLES = {
    "append-seq":       "Sequential Append (push_back N)",
    "append-random":    "Insert N at Random Positions",
    "random-access":    "M Random get(index) Lookups",
    "front-mutation":   "Front Insert + Front Erase (2N ops)",
    "random-mutation":  "Random Insert + Random Erase (2N ops)",
}
COLORS = {"TieredVector": "tab:blue", "AVLTree": "tab:red"}
MARKERS = {"TieredVector": "o", "AVLTree": "s"}

# ---- One execution-time figure per workload -------------------------------
for wl, title in TITLES.items():
    sub = df[df.workload == wl]
    if sub.empty:
        continue
    plt.figure(figsize=(6, 4.2))
    for struct in ["TieredVector", "AVLTree"]:
        s = sub[sub.structure == struct].sort_values("N")
        if s.empty:
            continue
        plt.plot(s.N, s.time_ms, marker=MARKERS[struct], color=COLORS[struct],
                 label=struct, linewidth=2)
    plt.xscale("log"); plt.yscale("log")
    plt.xlabel("N (number of elements)")
    plt.ylabel("Execution time (ms)")
    plt.title(title)
    plt.grid(True, which="both", ls=":", alpha=0.5)
    plt.legend()
    plt.tight_layout()
    path = os.path.join(OUT, f"time_{wl}.png")
    plt.savefig(path, dpi=130)
    plt.close()
    print("wrote", path)

# ---- Memory figure (only workloads that recorded memory) ------------------
mem = df[df.mem_MB >= 0]
mem = mem[mem.workload == "append-seq"]    # representative full-size structure
if not mem.empty:
    plt.figure(figsize=(6, 4.2))
    for struct in ["TieredVector", "AVLTree"]:
        s = mem[mem.structure == struct].sort_values("N")
        plt.plot(s.N, s.mem_MB, marker=MARKERS[struct], color=COLORS[struct],
                 label=struct, linewidth=2)
    plt.xscale("log"); plt.yscale("log")
    plt.xlabel("N (number of elements)")
    plt.ylabel("Memory (MB)")
    plt.title("Memory Footprint vs N (uint64 payload)")
    plt.grid(True, which="both", ls=":", alpha=0.5)
    plt.legend()
    plt.tight_layout()
    path = os.path.join(OUT, "memory.png")
    plt.savefig(path, dpi=130)
    plt.close()
    print("wrote", path)

print("Done. See ./plots/")
