# Roadmap

Mnemosyne is designed to grow incrementally. Each version builds directly on the previous one — no rewrites, only additions.

---

## v1 — Direct Match

**Goal:** A working, useful tool with the simplest possible search.

**Search strategy:** `strstr()` scan across all stored plain-text documents.

**Features**
- `mn add` — ingest `.txt`, `.md`
- `mn search` — case-insensitive whole-word content search and case-sensitive path segment search, with interactive result picker
- `mn list` — interactive picker over all indexed files
- `mn remove` — remove a file from the index
- `mn reindex` — re-parse every indexed file from disk
- `mn config ide` — configure which IDE to open files in
- Results ranked by recency, then match count
- 256-character context snippet centred on the first match

**Limitations**
- `.tex` ingestion not yet implemented
- No stemming: `"run"` does not match `"running"` or `"runs"`

---

## v2 — Inverted Index (current)

**Goal:** Make search fast regardless of index size.

**Search strategy:** Pre-built inverted index stored at `~/.mnemosyne/index/inverted.bin`. At ingest time each word is recorded with `(doc_id, position)` postings; at search time the query is tokenised, postings are looked up per token, and (for multi-word queries) positions are checked for adjacency so phrase queries still work. `mn search` then only opens the candidate docs — instead of scanning every file — to build the result snippet and verify case for `-c`.

**New features**
- `mn open` — workspaces: named sets of apps/files/URLs launched together, managed and opened via interactive pickers
- `.pdf` ingestion via `pdftotext` (poppler-utils) — bundled on Windows via `make fetch-poppler`, installed via package manager on macOS/Linux. See [file-types.md](file-types.md#pdf--pdf) for details.
- Case-insensitive search by default
- `-c` / `--case-sensitive` flag to opt back in
- `mn reindex` extended to also rebuild the inverted index from stored docs (the v1 command re-parses originals into `docs/`; v2 adds the inverted-index rebuild on top)
- `mn remove` keeps `inverted.bin` in sync by rebuilding after a removal
- `mn search` rebuilds `inverted.bin` automatically the first time it is missing or unreadable (e.g. upgrading from v1)
- Index stored as a compact binary file (`~/.mnemosyne/index/inverted.bin`)

**Breaking changes:** none — same commands, faster results.

---

## v3 — BM25 Ranking

**Goal:** Surface the most relevant results first, not just the most frequent ones.

**Search strategy:** Score each document with Okapi BM25 (Lucene variant), the ranking function used by every mainstream search engine (Lucene, Elasticsearch, tantivy, SQLite FTS5). BM25 extends TF-IDF with two crucial refinements: (a) term-frequency saturation — the 20th mention of a word contributes almost nothing more than the 5th, so repetition can't game the ranking; and (b) document-length normalisation — a short focused doc mentioning the query terms once beats a long doc that mentions them a few times amid unrelated content. Standard hyperparameters: `k1 = 1.2`, `b = 0.75`.

**Index changes**
- Per-document token count stored alongside the hash, needed for length normalisation.
- On-disk format bumped to v2 (magic still `MNIV`). Upgrading from v1 is automatic — the first `mn search` after upgrade prints a one-line notice and rebuilds `inverted.bin` from `docs/`.

**New features**
- Significantly better result ordering for multi-word queries.
- `mn search <query> --top N` to limit output to N results (follow-up PR).

**Deferred:** Stemming (`"run"` matching `"running"`) was scoped for v3 but held after implementation, pending real-world use of BM25 alone. May return in v3.1 or be dropped.

---

## v4 — Semantic Search (LLM-based)

**Goal:** Match by meaning, not just exact words.

**Search strategy:** Embed each document chunk and the query into a shared vector space using a lightweight on-device embedding model. Return results by cosine similarity.

**Approach**
- Use a small GGML-compatible embedding model (e.g. `nomic-embed-text`, `all-MiniLM-L6-v2`) via [llama.cpp](https://github.com/ggerganov/llama.cpp) or a thin wrapper
- Documents are chunked at ingest time (e.g. 512-token windows with 64-token overlap)
- Embeddings stored as float32 arrays in `~/.mnemosyne/index/vectors/`
- At search time: embed the query, compute cosine similarity against all vectors, rank results

**New commands**
- `mn model set <model-path>` — point to a local GGML model file
- `mn model status` — show currently loaded model

**New features**
- `mn search "what is the fastest sorting algorithm"` finds documents about sorting even if they never use those exact words
- `-e` / `--exact` flag to force v1-style keyword match instead

**System requirements**
- A GGML-compatible model file (~100 MB–4 GB depending on quality)
- CPU inference; GPU optional

---

## Future Ideas (unscheduled)

- `mn watch <dir>` — auto-index new files dropped into a folder
- `mn export` — export the full index as JSON
- OCR support for scanned PDFs (via Tesseract)
- Multi-language support
- Web UI (`mn serve`) for browser-based search
