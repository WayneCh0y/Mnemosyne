# Roadmap

Mnemosyne is designed to grow incrementally. Each version builds directly on the previous one — no rewrites, only additions.

---

## v1 — Direct Match (current)

**Goal:** A working, useful tool with the simplest possible search.

**Search strategy:** `strstr()` scan across all stored plain-text documents.

**Features**
- `mn add` — ingest `.txt`, `.md`
- `mn search` — case-insensitive whole-word content search and case-sensitive path segment search, with interactive result picker
- `mn list` — interactive picker over all indexed files
- `mn remove` — remove a file from the index
- `mn config ide` — configure which IDE to open files in
- Results ranked by recency, then match count
- 256-character context snippet centred on the first match

**Limitations**
- `.tex` and `.pdf` ingestion not yet implemented
- Linear scan: slow at very large index sizes (thousands of large documents)
- No stemming: `"run"` does not match `"running"` or `"runs"`

---

## v2 — Inverted Index

**Goal:** Make search fast regardless of index size.

**Search strategy:** Pre-built inverted index: `word → [doc_id, ...]`. Lookup is O(1) per term instead of O(n × file_size).

**New features**
- Case-insensitive search by default
- `-c` / `--case-sensitive` flag to opt back in
- `mn reindex` command to rebuild the inverted index from stored docs
- Index stored as a compact binary file (`~/.mnemosyne/index/inverted.bin`)

**Breaking changes:** none — same commands, faster results.

---

## v3 — TF-IDF Ranking

**Goal:** Surface the most relevant results first, not just the most frequent ones.

**Search strategy:** Score each document with TF-IDF (Term Frequency × Inverse Document Frequency). Documents that contain a rare query term score higher than documents that happen to contain a common word many times.

**New features**
- Significantly better result ordering for multi-word queries
- `mn search <query> --top N` to limit output to N results
- Stemming support (e.g. `"run"` matches `"running"`, `"runs"`)

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
