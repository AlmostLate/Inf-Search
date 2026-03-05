#!/usr/bin/env python3

import asyncio
import hashlib
import logging
import os
import re
import sqlite3
import sys
import time
import xml.etree.ElementTree as ET
from datetime import datetime
from urllib.parse import urljoin, urlparse

import aiohttp
import yaml
from bs4 import BeautifulSoup

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger(__name__)

SITEMAP_NS = {"sm": "http://www.sitemaps.org/schemas/sitemap/0.9"}


def load_config(path):
    with open(path, "r") as f:
        return yaml.safe_load(f)


def normalize_url(url):
    parsed = urlparse(url)
    path = parsed.path.rstrip("/")
    return f"{parsed.scheme}://{parsed.netloc}{path}"


def content_hash(text):
    return hashlib.md5(text.encode("utf-8", errors="replace")).hexdigest()


def init_db(db_path):
    os.makedirs(os.path.dirname(db_path) if os.path.dirname(db_path) else ".", exist_ok=True)
    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=NORMAL")
    conn.execute("""
        CREATE TABLE IF NOT EXISTS documents (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            url             TEXT UNIQUE NOT NULL,
            source          TEXT NOT NULL,
            crawl_timestamp INTEGER NOT NULL,
            content_hash    TEXT NOT NULL,
            title           TEXT,
            text_bytes      INTEGER DEFAULT 0
        )
    """)
    conn.execute("CREATE INDEX IF NOT EXISTS idx_url ON documents(url)")
    conn.execute("CREATE INDEX IF NOT EXISTS idx_source ON documents(source)")
    conn.commit()
    return conn


def db_count(conn):
    return conn.execute("SELECT COUNT(*) FROM documents").fetchone()[0]


def get_existing_urls(conn, source, recrawl_before):
    rows = conn.execute(
        "SELECT url FROM documents WHERE source=? AND crawl_timestamp>?",
        (source, recrawl_before),
    ).fetchall()
    return {r[0] for r in rows}


def extract_text(html, source):
    soup = BeautifulSoup(html, "lxml")
    for tag in soup(["script", "style", "nav", "header", "footer",
                     "aside", "iframe", "noscript", "svg", "form"]):
        tag.decompose()

    title = ""
    h1 = soup.find("h1")
    if h1:
        title = h1.get_text(strip=True)
    if not title:
        tt = soup.find("title")
        if tt:
            title = tt.get_text(strip=True)

    body = ""
    if source == "livescience":
        el = soup.find("article") or soup.find("div", {"id": "article-body"})
        if el:
            body = el.get_text(separator=" ", strip=True)
    elif source == "sciencealert":
        el = soup.find("article") or soup.find("div", class_="article-body")
        if el:
            body = el.get_text(separator=" ", strip=True)
    elif source == "space":
        el = soup.find("article") or soup.find("div", {"id": "article-body"})
        if el:
            body = el.get_text(separator=" ", strip=True)

    if not body:
        main = (soup.find("main") or soup.find("article")
                or soup.find("div", {"role": "main"})
                or soup.find("div", class_="content"))
        if main:
            body = main.get_text(separator=" ", strip=True)
        else:
            body = soup.get_text(separator=" ", strip=True)

    body = " ".join(body.split())
    return title, body


async def fetch(session, url, semaphore, delay=0.0):
    async with semaphore:
        if delay > 0:
            await asyncio.sleep(delay)
        try:
            async with session.get(url, timeout=aiohttp.ClientTimeout(total=30)) as resp:
                if resp.status == 200:
                    return await resp.text(errors="replace")
        except Exception:
            pass
    return None


async def fetch_sitemap_urls(session, sitemap_url, semaphore):
    urls = []
    xml_text = await fetch(session, sitemap_url, semaphore)
    if not xml_text:
        log.warning("Cannot fetch sitemap: %s", sitemap_url)
        return urls
    try:
        root = ET.fromstring(xml_text)
    except ET.ParseError:
        log.warning("Cannot parse sitemap XML: %s", sitemap_url)
        return urls

    sitemapindex = root.findall("sm:sitemap", SITEMAP_NS)
    if sitemapindex:
        tasks = []
        for sm in sitemapindex:
            loc = sm.find("sm:loc", SITEMAP_NS)
            if loc is not None and loc.text:
                tasks.append(fetch_sitemap_urls(session, loc.text.strip(), semaphore))
        results = await asyncio.gather(*tasks)
        for r in results:
            urls.extend(r)
    else:
        for url_el in root.findall("sm:url", SITEMAP_NS):
            loc = url_el.find("sm:loc", SITEMAP_NS)
            if loc is not None and loc.text:
                urls.append(loc.text.strip())
    return urls


async def discover_sciencedaily_urls(session, base_url, semaphore, target):
    urls = []
    now = datetime.now()
    year, month = now.year, now.month

    while len(urls) < target and year >= 2010:
        archive_url = f"{base_url}{year}/{month:02d}/"
        html = await fetch(session, archive_url, semaphore)
        if html:
            soup = BeautifulSoup(html, "lxml")
            for a in soup.select("a[href]"):
                href = a["href"]
                if "/releases/" in href and href.endswith(".htm"):
                    urls.append(urljoin(base_url, href))
        month -= 1
        if month < 1:
            month = 12
            year -= 1
        if len(urls) % 2000 == 0 and urls:
            log.info("ScienceDaily: %d URLs (at %d/%02d)", len(urls), year, month)

    seen = set()
    unique = []
    for u in urls:
        n = normalize_url(u)
        if n not in seen:
            seen.add(n)
            unique.append(n)
    return unique[:target]


def filter_article_urls(urls, domain):
    skip = ["/tag/", "/author/", "/category/", "/page/", "/video/",
            "/about", "/contact", "/privacy", "/terms", "/newsletter",
            "/search", "/sitemap", "/feed", "/wp-json", "/cdn-cgi",
            "/deals/", "/coupons/", "/buying-guide"]
    result = []
    for u in urls:
        path = urlparse(u).path.lower()
        if any(p in path for p in skip):
            continue
        if path in ("/", ""):
            continue
        if domain in ("livescience.com", "space.com"):
            if re.search(r"/[a-z]+-[a-z]", path) or re.search(r"/\d+", path):
                result.append(u)
        elif domain == "sciencealert.com":
            if len(path.split("/")) >= 2 and path != "/":
                result.append(u)
        else:
            result.append(u)
    return result


async def crawl_source(session, conn, corpus_dir, source_cfg, logic_cfg, semaphore):
    name   = source_cfg["name"]
    target = source_cfg["target"]
    domain = source_cfg["domain"]
    min_len = logic_cfg.get("min_text_length", 200)

    log.info("=== Discovering URLs for [%s] ===", name)

    if name == "sciencedaily":
        all_urls = await discover_sciencedaily_urls(
            session, source_cfg["base_url"], semaphore, target)
    else:
        all_urls = await fetch_sitemap_urls(session, source_cfg["sitemap"], semaphore)

    all_urls = filter_article_urls(all_urls, domain)
    log.info("%s: %d candidate URLs (target: %d)", name, len(all_urls), target)
    all_urls = all_urls[:target]

    recrawl_before = int(time.time()) - logic_cfg["recrawl_days"] * 86400
    existing = get_existing_urls(conn, name, recrawl_before)
    to_crawl = [u for u in all_urls if normalize_url(u) not in existing]
    log.info("%s: %d to crawl (%d already in DB)", name, len(to_crawl), len(existing))

    if not to_crawl:
        return 0

    saved = 0
    batch_size = logic_cfg["batch_size"]
    delay = logic_cfg["delay"]
    next_id = db_count(conn)

    for i in range(0, len(to_crawl), batch_size):
        batch = to_crawl[i : i + batch_size]
        tasks = [fetch(session, u, semaphore, delay) for u in batch]
        results = await asyncio.gather(*tasks)

        for url, html in zip(batch, results):
            if not html or len(html) < 500:
                continue

            title, body = extract_text(html, name)
            if len(body) < min_len:
                continue

            norm = normalize_url(url)
            h = content_hash(body)
            text_bytes = len(body.encode("utf-8"))

            fname = f"{next_id:06d}.txt"
            fpath = os.path.join(corpus_dir, fname)
            with open(fpath, "w", encoding="utf-8") as f:
                f.write(title + "\n")
                f.write(norm + "\n")
                f.write(body + "\n")

            try:
                conn.execute(
                    """INSERT INTO documents (url, source, crawl_timestamp,
                       content_hash, title, text_bytes)
                       VALUES (?,?,?,?,?,?)
                       ON CONFLICT(url) DO UPDATE SET
                           crawl_timestamp=excluded.crawl_timestamp,
                           content_hash=excluded.content_hash""",
                    (norm, name, int(time.time()), h, title, text_bytes),
                )
                saved += 1
                next_id += 1
            except Exception as e:
                log.warning("DB error %s: %s", norm, e)

        conn.commit()

        bn = i // batch_size + 1
        tb = (len(to_crawl) + batch_size - 1) // batch_size
        log.info("%s: batch %d/%d  saved=%d  total=%d",
                 name, bn, tb, saved, db_count(conn))

    return saved


async def main(config_path):
    cfg = load_config(config_path)
    logic = cfg["logic"]

    base_dir = os.path.dirname(os.path.abspath(config_path))
    db_path = os.path.join(base_dir, cfg["db"]["path"])
    conn = init_db(db_path)
    log.info("DB: %s  (existing: %d docs)", db_path, db_count(conn))

    corpus_dir = os.path.join(base_dir, "..", "corpus")
    os.makedirs(corpus_dir, exist_ok=True)

    headers = {"User-Agent": logic["user_agent"]}
    connector = aiohttp.TCPConnector(limit=logic["max_concurrent"], ssl=False)
    semaphore = asyncio.Semaphore(logic["max_concurrent"])

    async with aiohttp.ClientSession(headers=headers, connector=connector) as session:
        for src in cfg["sources"]:
            n = await crawl_source(session, conn, corpus_dir, src, logic, semaphore)
            log.info("[%s] done: +%d new docs", src["name"], n)

    total = db_count(conn)
    log.info("=== Crawl complete. Total: %d documents ===", total)
    conn.close()


if __name__ == "__main__":
    config_path = sys.argv[1] if len(sys.argv) > 1 else "config.yaml"
    asyncio.run(main(config_path))
