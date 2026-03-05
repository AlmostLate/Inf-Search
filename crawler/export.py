#!/usr/bin/env python3

import csv
import os
import sqlite3
import sys

import yaml
from bs4 import BeautifulSoup
from tqdm import tqdm


def load_config(path):
    with open(path, "r") as f:
        return yaml.safe_load(f)


def extract_text(html, source):
    soup = BeautifulSoup(html, "lxml")

    for tag in soup(["script", "style", "nav", "header", "footer",
                     "aside", "iframe", "noscript", "svg", "form"]):
        tag.decompose()

    title_tag = soup.find("title")
    title = title_tag.get_text(strip=True) if title_tag else ""

    h1 = soup.find("h1")
    if h1:
        title = h1.get_text(strip=True)

    body = ""
    if source == "livescience":
        article = soup.find("article") or soup.find("div", {"id": "article-body"})
        if article:
            body = article.get_text(separator=" ", strip=True)
    elif source == "sciencealert":
        article = soup.find("article") or soup.find("div", class_="article-body")
        if article:
            body = article.get_text(separator=" ", strip=True)
    elif source == "space":
        article = soup.find("article") or soup.find("div", {"id": "article-body"})
        if article:
            body = article.get_text(separator=" ", strip=True)

    if not body:
        main = (
            soup.find("main")
            or soup.find("article")
            or soup.find("div", {"role": "main"})
            or soup.find("div", class_="content")
        )
        if main:
            body = main.get_text(separator=" ", strip=True)
        else:
            body = soup.get_text(separator=" ", strip=True)

    body = " ".join(body.split())
    return title, body


def main(config_path):
    cfg = load_config(config_path)
    base_dir = os.path.dirname(os.path.abspath(config_path))
    db_path = os.path.join(base_dir, cfg["db"]["path"])

    if not os.path.exists(db_path):
        print(f"Database not found: {db_path}. Run crawler.py first.")
        sys.exit(1)

    conn = sqlite3.connect(db_path)
    total_in_db = conn.execute("SELECT COUNT(*) FROM documents").fetchone()[0]
    print(f"Total documents in DB: {total_in_db}")

    out_dir = os.path.join(base_dir, "..", "corpus")
    os.makedirs(out_dir, exist_ok=True)

    min_len = cfg["logic"].get("min_text_length", 200)

    meta_path = os.path.join(out_dir, "metadata.csv")
    meta_file = open(meta_path, "w", newline="", encoding="utf-8")
    writer = csv.writer(meta_file)
    writer.writerow(["id", "url", "title", "source", "text_bytes", "raw_bytes"])

    doc_id = 0
    total_raw_bytes = 0
    total_text_bytes = 0
    source_counts = {}

    cur = conn.execute("SELECT url, raw_html, source FROM documents")
    for url, raw_html, source in tqdm(cur, total=total_in_db, desc="Exporting"):
        title, body = extract_text(raw_html, source)

        if len(body) < min_len:
            continue

        raw_bytes = len(raw_html.encode("utf-8", errors="replace"))
        text_bytes = len(body.encode("utf-8", errors="replace"))

        fname = f"{doc_id:06d}.txt"
        fpath = os.path.join(out_dir, fname)
        with open(fpath, "w", encoding="utf-8") as out:
            out.write(title + "\n")
            out.write(url + "\n")
            out.write(body + "\n")

        writer.writerow([doc_id, url, title, source, text_bytes, raw_bytes])

        total_raw_bytes += raw_bytes
        total_text_bytes += text_bytes
        source_counts[source] = source_counts.get(source, 0) + 1
        doc_id += 1

    meta_file.close()
    conn.close()

    exported = doc_id
    avg_text = total_text_bytes / exported if exported else 0
    avg_raw = total_raw_bytes / exported if exported else 0

    stats_path = os.path.join(out_dir, "stats.txt")
    with open(stats_path, "w", encoding="utf-8") as f:
        f.write(f"Total documents exported: {exported}\n")
        f.write(f"Total raw HTML size: {total_raw_bytes / (1024**3):.2f} GB\n")
        f.write(f"Total extracted text size: {total_text_bytes / (1024**2):.1f} MB\n")
        f.write(f"Average raw HTML per doc: {avg_raw / 1024:.1f} KB\n")
        f.write(f"Average text per doc: {avg_text:.0f} bytes\n")
        f.write(f"\nPer-source counts:\n")
        for src, cnt in sorted(source_counts.items()):
            f.write(f"  {src}: {cnt}\n")

    print(f"\nExported {exported} documents to {out_dir}")
    print(f"Raw HTML: {total_raw_bytes / (1024**3):.2f} GB")
    print(f"Text: {total_text_bytes / (1024**2):.1f} MB")
    print(f"Avg text/doc: {avg_text:.0f} bytes")
    for src, cnt in sorted(source_counts.items()):
        print(f"  {src}: {cnt}")


if __name__ == "__main__":
    config_path = sys.argv[1] if len(sys.argv) > 1 else "config.yaml"
    main(config_path)
