#!/usr/bin/env python3

import csv
import os
import sys


def main(corpus_dir):
    meta_path = os.path.join(corpus_dir, "metadata.csv")
    if not os.path.exists(meta_path):
        print("metadata.csv not found. Run export.py first.")
        sys.exit(1)

    total_docs = 0
    total_text = 0
    total_raw = 0
    sources = {}

    with open(meta_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            total_docs += 1
            tb = int(row["text_bytes"])
            rb = int(row["raw_bytes"])
            total_text += tb
            total_raw += rb
            src = row["source"]
            sources[src] = sources.get(src, 0) + 1

    print(f"Documents: {total_docs}")
    print(f"Raw HTML:  {total_raw / (1024**3):.2f} GB")
    print(f"Text:      {total_text / (1024**2):.1f} MB")
    print(f"Avg raw:   {total_raw / total_docs / 1024:.1f} KB")
    print(f"Avg text:  {total_text / total_docs:.0f} bytes")
    print("\nSources:")
    for src, cnt in sorted(sources.items()):
        print(f"  {src}: {cnt}")


if __name__ == "__main__":
    corpus_dir = sys.argv[1] if len(sys.argv) > 1 else "../corpus"
    main(corpus_dir)
