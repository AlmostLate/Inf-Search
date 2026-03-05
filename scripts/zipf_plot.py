#!/usr/bin/env python3

import sys
from collections import Counter

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def main():
    if len(sys.argv) < 3:
        print("Usage: python zipf_plot.py <tokens_file> <output_png>")
        sys.exit(1)

    tokens_path = sys.argv[1]
    output_path = sys.argv[2]

    print("Reading tokens...")
    freq = Counter()
    total = 0
    with open(tokens_path, "r", encoding="utf-8") as f:
        for line in f:
            tok = line.strip()
            if tok:
                freq[tok] += 1
                total += 1

    print(f"Total tokens: {total}")
    print(f"Unique tokens: {len(freq)}")

    ranked = freq.most_common()
    ranks = np.arange(1, len(ranked) + 1, dtype=float)
    frequencies = np.array([c for _, c in ranked], dtype=float)

    c_zipf = frequencies[0]
    zipf_theoretical = c_zipf / ranks

    fig, ax = plt.subplots(figsize=(10, 7))
    ax.loglog(ranks, frequencies, linewidth=0.8, label="Observed", color="#2E86AB")
    ax.loglog(ranks, zipf_theoretical, linewidth=1.2, linestyle="--",
              label=f"Zipf (C/r, C={c_zipf:.0f})", color="#E8430C")

    ax.set_xlabel("Rank (log scale)", fontsize=12)
    ax.set_ylabel("Frequency (log scale)", fontsize=12)
    ax.set_title("Term Frequency Distribution vs Zipf's Law\n(Pop-Sci Corpus)", fontsize=14)
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)

    top10 = ranked[:10]
    info = "Top-10 tokens:\n" + "\n".join(
        f"  {r+1}. {tok} ({cnt:,})" for r, (tok, cnt) in enumerate(top10)
    )
    ax.text(0.98, 0.98, info, transform=ax.transAxes, fontsize=8,
            verticalalignment="top", horizontalalignment="right",
            fontfamily="monospace",
            bbox=dict(boxstyle="round,pad=0.4", facecolor="white", alpha=0.85))

    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    print(f"Plot saved to {output_path}")


if __name__ == "__main__":
    main()
