#!/usr/bin/env python3

import json
import os
import subprocess

from flask import Flask, render_template, request

app = Flask(__name__)

BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SEARCH_CLI = os.environ.get("SEARCH_CLI", os.path.join(BASE_DIR, "core", "search_cli"))
INDEX_FILE = os.environ.get("INDEX_FILE", os.path.join(BASE_DIR, "index.bin"))

PAGE_SIZE = 50


def run_search(query):
    try:
        proc = subprocess.run(
            [SEARCH_CLI, INDEX_FILE, "--json"],
            input=query + "\n",
            capture_output=True,
            text=True,
            timeout=10,
        )
        if proc.returncode != 0:
            return {"query": query, "count": 0, "time_ms": 0, "results": [], "error": proc.stderr}
        line = proc.stdout.strip()
        if not line:
            return {"query": query, "count": 0, "time_ms": 0, "results": []}
        return json.loads(line)
    except Exception as e:
        return {"query": query, "count": 0, "time_ms": 0, "results": [], "error": str(e)}


@app.route("/")
def index():
    return render_template("search.html")


@app.route("/search")
def search():
    query = request.args.get("q", "").strip()
    page = int(request.args.get("page", 1))
    if not query:
        return render_template("search.html")

    data = run_search(query)
    results = data.get("results", [])
    total = data.get("count", 0)
    time_ms = data.get("time_ms", 0)

    start = (page - 1) * PAGE_SIZE
    end = start + PAGE_SIZE
    page_results = results[start:end]
    has_next = end < len(results)
    has_prev = page > 1

    return render_template(
        "results.html",
        query=query,
        results=page_results,
        total=total,
        time_ms=time_ms,
        page=page,
        has_next=has_next,
        has_prev=has_prev,
    )


if __name__ == "__main__":
    app.run(debug=True, host="0.0.0.0", port=5050)
