## 4.4BSD B-Tree implementation (reference)

This folder contains a **reference copy** of the historical **4.4BSD-Lite2**
`btree` + `mpool` implementation (Berkeley DB 1.85 era).

- **Upstream**: `sergev/4.4BSD-Lite2` (GitHub)
- **Imported via**: `git clone --depth 1 --sparse` + `git sparse-checkout set ...`
- **Upstream commit (local checkout)**: `50587b0` (see `.git` in this folder)

### What is included

- `usr/src/lib/libc/db/btree/` (btree source files)
- `usr/src/lib/libc/db/mpool/` (mpool source files)
- `usr/src/lib/libc/db/PORT/include/` (historical `db.h`, `mpool.h`, etc.)

### What this is used for

This code is **not** built as part of tiny-clj. It is kept only as a
**read-only reference** for algorithms, data layout, and corner cases while
implementing Flash-Tree.

### License

See the original Berkeley copyright headers inside the files and
`COPYRIGHT` in this directory.

