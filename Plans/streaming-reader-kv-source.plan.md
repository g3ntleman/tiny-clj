---
name: Streaming Reader für KV-Source-Loading
overview: >
  Erweitert den Reader um einen optionalen Refill-Callback, sodass beim require
  von Namespaces aus dem eigenen KV-Filesystem die Quelldatei nicht mehr vollständig
  ins RAM geladen wird. Der bestehende 4-KB-FS-Block-Buffer (FS_STORE_CHUNK_SIZE = 4096)
  lebt im KvReaderCtx auf dem Stack; der Reader zeigt direkt darauf — kein separater
  Puffer im Reader-Struct, kein memcpy in den Reader. Refill wird proaktiv in
  reader_next ausgelöst (Overlap = 4 Bytes, deckt UTF-8 + #| ab). Embedded-Sources
  (ROM-Pointer) bleiben unverändert zero-copy. Test-first, schrittweise.
todos:
  - id: step-1a-refill-test
    content: >
      Test (src/tests/test_reader_stream.c): Reader mit Fake-Refill-Callback,
      der einen externen char-Puffer chunk-weise befüllt (kein Reader-interner Buffer).
      Zwei Chunks à 8 Bytes: "Hello, W" + "orld!\n".
      Prüft: reader_current/reader_next liefern korrekte Zeichen über Chunk-Grenze;
      reader_is_eof() true erst nach '\n'.
    status: todo

  - id: step-1b-peek-ahead-boundary-test
    content: >
      Test: letztes Byte von Chunk 1 ist '#', erstes Byte von Chunk 2 ist '|'.
      reader_skip_block_comment erkennt die #| Sequenz korrekt.
      Sicherstellt: Overlap-Mechanismus (4 Bytes) funktioniert über Grenze.
    status: todo

  - id: step-1c-utf8-boundary-test
    content: >
      Test: 2-Byte-UTF-8-Zeichen (U+00E4 = 0xC3 0xA4) liegt auf Chunk-Grenze
      (0xC3 am Ende von Chunk 1, 0xA4 am Anfang von Chunk 2).
      reader_next_codepoint liefert U+00E4 korrekt, kein Overread.
    status: todo

  - id: step-1d-eof-test
    content: >
      Test: Refill-Callback liefert 0 neue Bytes → refill_fn wird auf NULL gesetzt.
      reader_is_eof() true nach Verbrauch verbleibender Overlap-Bytes.
      Weitere next/peek-Aufrufe kein Absturz.
    status: todo

  - id: step-2-reader-struct-refill
    content: >
      Impl: Reader-Struct (reader.h) um minimale optionale Felder erweitern.
      Zero-init = kein Streaming, alle ~30 bestehenden Aufrufer unverändert.
      Kein CljString-Parameter: reader.h bleibt frei von Runtime-Dependencies
      (nur stddef/stdbool/stdint/utf8.h). Lifetime-Management bleibt beim Caller.

      Neue Felder in struct Reader (am Ende, zero-init = kein Streaming):
        bool (*refill_fn)(struct Reader_ *r, void *ctx);  // NULL = statischer Puffer
        void  *refill_ctx;
        size_t global_offset;  // Bytes konsumiert vor aktuellem Fenster

      Kein refill_buf im Struct — Buffer gehört zum jeweiligen Ctx (Stack).

      Neuer Init (reader.h + reader.c):
        // Generischer Streaming-Init. refill_fn-Vertrag: siehe Abschnitt unten.
        // Führt initialen Refill durch (src = ctx-buf, length = erstes Chunk).
        void reader_init_with_refill(Reader *r,
                                     bool (*refill_fn)(Reader *r, void *ctx),
                                     void *ctx);

      reader_next (reader.c): Refill proaktiv auslösen wenn Fenster knapp:
        #define READER_REFILL_AHEAD 4
        if (r->refill_fn && r->length - r->index < READER_REFILL_AHEAD)
            r->refill_fn(r, r->refill_ctx);
        // Danach unverändert: if (index >= length) return '\0';

      reader_offset (reader.c): return r->global_offset + r->index;
        (Stuck-Detection in eval_source_buffer_in_current_state korrekt.)

      reader_is_eof / reader_eof: unverändert (index >= length).
        Kein Refill-Trigger nötig — reader_next refilled proaktiv,
        initialer Refill in reader_init_with_refill füllt den ersten Block.

      Bestehende Init-Funktionen setzen refill_fn = NULL (zero-init):
        reader_init, reader_init_with_length, reader_init_with_source → unverändert.
    status: todo

  - id: step-3a-fs-read-chunk-api
    content: >
      Impl: Neue C-only Funktion in fs_layer (NICHT Teil der Clojure-API):
        tdb_status_t fs_file_read_chunk(FsKvStore *st, const char *path,
                                        size_t offset, uint8_t *buf, size_t buf_size,
                                        size_t *out_bytes_read);
      Liest genau einen FS-Block (max buf_size Bytes) ab Offset. Nutzt intern
      fs_kv_stream_read_key_bytes_from mit einem Sentinel-Stop-Mechanismus:
      sink_cb füllt buf bis buf_size, gibt dann TDB_ERR_NO_DATA zurück um
      Streaming zu stoppen. Caller behandelt TDB_ERR_NO_DATA als Erfolg.
      *out_bytes_read = 0 → EOF.
      Warum eigene Funktion statt fs_file_stream_read_from direkt: die
      vorhandene Streaming-API liest bis EOF über mehrere Callbacks, wir
      brauchen aber "genau einen Block und Stop".
    status: todo

  - id: step-3b-kv-reader-ctx-test
    content: >
      Test: FsKvStore mit Datei (Inhalt = 2x FS_STORE_CHUNK_SIZE = 8192 Bytes).
      KvReaderCtx aufgebaut, reader_init_with_kv_stream aufgerufen.
      Alle Zeichen via reader_next korrekt lesbar bis EOF.
      Heap-Messung: kein malloc für Source-Bytes (nur Stack-Delta für KvReaderCtx).
    status: todo

  - id: step-3c-kv-reader-ctx-impl
    content: >
      Impl: KvReaderCtx-Struct und reader_init_with_kv_stream (neue Datei
      src/kv_reader.h + src/kv_reader.c):

        #define KV_READER_BUF_SIZE FS_STORE_CHUNK_SIZE  // = 4096

        typedef struct {
            FsKvStore  *st;
            const char *path;
            size_t      file_offset;   // nächster Lese-Offset in Datei
        } KvReaderCtx;

        // kv_reader_refill: wird von Reader als refill_fn gesetzt
        static bool kv_reader_refill(Reader *r, void *ctx_) {
            KvReaderCtx *ctx = ctx_;
            // 1. Overlap: verbleibende Bytes (max 4) an Puffer-Anfang schieben
            size_t overlap = (r->length > r->index) ? r->length - r->index : 0;
            if (overlap > READER_REFILL_AHEAD) overlap = READER_REFILL_AHEAD;
            uint8_t *buf = (uint8_t *)r->src - overlap; // buf = ctx->buf (Trick via Pointer)
            // Tatsächlich: buf zeigt immer auf ctx->buf (vom Caller gesetzt)
            if (overlap) memmove(buf, r->src + r->index, overlap);
            r->global_offset += r->index;
            // 2. Nächsten Block laden
            size_t bytes_read = 0;
            tdb_status_t st = fs_file_read_chunk(ctx->st, ctx->path,
                                                  ctx->file_offset,
                                                  buf + overlap,
                                                  KV_READER_BUF_SIZE - overlap,
                                                  &bytes_read);
            ctx->file_offset += bytes_read;
            r->src    = (const char *)buf;
            r->length = overlap + bytes_read;
            r->index  = 0;
            if (bytes_read == 0) r->refill_fn = NULL;  // EOF: keine weiteren Refills
            return (r->length > 0);
        }

        // KvReaderCtx liegt beim Caller auf dem Stack; buf[] ebenfalls:
        void reader_init_with_kv_stream(Reader *r, KvReaderCtx *ctx,
                                        uint8_t *buf /* KV_READER_BUF_SIZE */) {
            memset(r, 0, sizeof(*r));
            r->line        = 1;
            r->column      = 1;
            r->refill_fn   = kv_reader_refill;
            r->refill_ctx  = ctx;
            r->src         = (const char *)buf;
            r->length      = 0;
            r->index       = 0;
            // Initialer Refill füllt den ersten Block
            kv_reader_refill(r, ctx);
        }

      Caller-Seite (load_namespace_from_kv_stream):
        uint8_t buf[KV_READER_BUF_SIZE];   // 4 KB, Stack
        KvReaderCtx kv_ctx = { .st = fst, .path = source_path };
        Reader reader;
        reader_init_with_kv_stream(&reader, &kv_ctx, buf);
    status: todo

  - id: step-4a-ns-load-stream-test
    content: >
      Test: Namespace "(ns test.stream-ns)\n(def x 42)\n" in KV-Store.
      Testweise FS_STORE_CHUNK_SIZE auf 16 setzen (Testkonstante) damit
      Chunk-Grenzen mitten im Source liegen. load_namespace_from_kv_stream
      aufrufen. Prüfen: x = 42. Keine Heap-Allokation für Source-Bytes.
    status: todo

  - id: step-4b-ns-load-stream-impl
    content: >
      Impl: eval_source_with_reader(Reader *r, const char *src_name, EvalState *st)
      als innere Funktion — extrahiert aus eval_source_buffer_in_current_state
      (die eval-Loop: while not eof, parse, eval, WITH_AUTORELEASE_POOL).

      eval_source_buffer_in_current_state: baut on-stack Reader via
      reader_init_with_length, ruft eval_source_with_reader auf. Kein
      Verhaltensunterschied für bestehende Aufrufer.

      load_namespace_from_kv_stream(EvalState *st, const char *ns_name,
                                     FsKvStore *fst, const char *source_path):
        uint8_t buf[KV_READER_BUF_SIZE];
        KvReaderCtx kv_ctx = { .st = fst, .path = source_path };
        Reader reader;
        reader_init_with_kv_stream(&reader, &kv_ctx, buf);
        // ... ns setup (wie load_namespace_from_bytes) ...
        return eval_source_with_reader(&reader, source_path, st);
    status: todo

  - id: step-5a-require-wire-test
    content: >
      Test: (require 'test.stream-ns) aus KV-Store. Heap-Messung vor/nach:
      Delta = nur Namespace-Inhalt (Closures, Vars), nicht Source-Bytes (4 KB).
    status: todo

  - id: step-5b-require-wire-impl
    content: >
      Impl: In load_namespace_source (require-Pfad), vor resolve_path_to_bytes:
        FsKvStore *fst = fs_global_store_if_initialized();
        if (fst && fs_exists(fst, source_path))
            return load_namespace_from_kv_stream(st, ns_name, fst, source_path);
      Danach unverändert: Embedded-Source → ByteArray-View → reader_init_with_length.
      KV hat Vorrang (User-Override-Semantik bleibt).
    status: todo

  - id: step-6-cleanup
    content: >
      fs_read_bytes aus require-Pfad entfernen. load_namespace_from_bytes bleibt
      für externe Aufrufer (Tests, die bereits Bytes haben). Alle bestehenden
      require/load/embedded-Tests grün.
    status: todo
---

## Kontext

Aktueller Ablauf beim `require` aus KV-Store:

```
fs_read_bytes(st, path)          // gesamte Datei → ByteArray malloc (z.B. 8 KB)
held_bytes = RETAIN(ba)          // hält Source im DRAM für gesamte Ladezeit
eval_source_buffer_in_current_state(ba->data, ba->length, ...)
    └── while not EOF:
          parse one form          // form-by-form bereits ✓
          eval form               // AST nach jeder Form freigegeben ✓
RELEASE(held_bytes)               // Source erst hier freigegeben ✗
```

Ziel:

```
uint8_t buf[4096];                // Stack, kein malloc
KvReaderCtx kv_ctx = { st, path };
reader_init_with_kv_stream(&reader, &kv_ctx, buf);
eval_source_with_reader(&reader, ...)
    └── while not EOF:
          reader_next → Refill bei < 4 Bytes Reste (fs_file_read_chunk)
          parse one form          // max 4 KB gleichzeitig im buf ✓
          eval form
```

Peak-DRAM für Source = `KV_READER_BUF_SIZE` (4 KB, Stack) statt Dateigröße.

## Generische Reader-API

### Design-Entscheidungen

**Kein `CljString`-Parameter:**
`reader.h` bleibt eine reine C-Bibliothek ohne Runtime-Abhängigkeit
(nur `stddef`/`stdbool`/`stdint`/`utf8.h`). Gründe:
- Reader wird vor der vollständigen Runtime genutzt (Bootstrap, Tests)
- `CljString` hätte RETAIN/RELEASE im Reader nötig — kein Destruktor vorhanden
- Streaming-Quellen (KV, UART, Netz) haben keinen `CljString`
- Richtige Abstraktion: Caller hält Lifetime, Reader ist View ohne Ownership

```c
// Caller-Muster für statische Quellen — heute und künftig:
ID held = RETAIN(resolve_path_to_bytes(path));   // ByteArray-View (kein Copy)
reader_init_with_length(&r, (const char *)ba->data, (size_t)ba->length);
// ... eval ...
RELEASE(held);
```

**`reader_init_with_refill` als generischer Erweiterungspunkt:**

```c
// Deckt alle Streaming-Use-Cases ab — kein weiterer Init nötig:
//   KV-Store:  reader_init_with_refill(r, kv_reader_refill,   &kv_ctx)
//   UART:      reader_init_with_refill(r, uart_reader_refill, &uart_ctx)
//   Netzwerk:  reader_init_with_refill(r, net_reader_refill,  &net_ctx)
//   REPL-Zeil: reader_init_with_refill(r, line_reader_refill, &line_ctx)
void reader_init_with_refill(Reader *r,
                              bool (*refill_fn)(Reader *r, void *ctx),
                              void *ctx);
```

**`refill_fn`-Vertrag:**
```
Aufruf-Bedingung: r->length - r->index < READER_REFILL_AHEAD (proaktiv in reader_next)
                  ODER: initialer Aufruf in reader_init_with_refill (index=0, length=0)

Refill-Funktion muss:
  1. r->global_offset += r->index          // Verbrauchte Bytes festhalten
  2. overlap = min(r->length - r->index, READER_REFILL_AHEAD)
     memmove(ctx->buf, r->src + r->index, overlap)  // Lookahead sichern
  3. Neue Bytes nach ctx->buf[overlap] laden (Quelle: KV/UART/Netz/...)
  4. r->src    = (const char *)ctx->buf
     r->length = overlap + new_bytes
     r->index  = 0
  5. Bei EOF (new_bytes == 0): r->refill_fn = NULL
  6. return (r->length > 0)

Buffer-Ownership: liegt beim ctx (Stack), NICHT beim Reader.
Reader ist nach wie vor ein View (src = Fremdpuffer).
```

**Use-Case-Übersicht:**

| Quelle          | Init                          | Puffer liegt in       |
|-----------------|-------------------------------|-----------------------|
| C-String        | `reader_init(r, src)`         | statisch / Stack      |
| ROM / Embedded  | `reader_init_with_length(...)`| Flash (zero-copy)     |
| KV-Store        | `reader_init_with_refill(...)`| `KvReaderCtx.buf` Stack |
| UART / Netz     | `reader_init_with_refill(...)`| plattformspez. Ctx    |
| REPL zeilenweise| `reader_init_with_refill(...)`| Zeilenpuffer-Ctx      |

## Technische Details

### Refill-Trigger in reader_next

```
Trigger: r->length - r->index < READER_REFILL_AHEAD (= 4)
Aktion:  overlap = min(remaining, 4) Bytes an Pufferstart schieben (memmove)
         fs_file_read_chunk → neue Bytes dahinter
         r->src = buf, r->index = 0, r->length = overlap + new_bytes
         r->global_offset += consumed
Falls new_bytes == 0: r->refill_fn = NULL (kein Refill mehr)
```

Invariante: nach jedem Refill sind mindestens `READER_REFILL_AHEAD` Bytes
gültig (oder echtes EOF), damit decken wir ab:
- `reader_peek_ahead(r, 1)` für `#|` block-comment (bereits bounds-safe: gibt '\0' zurück)
- `utf8codepoint` liest bis zu 4 Bytes (gegen Overread geschützt)
- 4-Byte-UTF-8-Sequenz auf Chunk-Grenze: Overlap trägt beide Hälften

### KvReaderCtx: buf-Pointer-Problem

`kv_reader_refill` braucht Zugriff auf den 4-KB-Puffer für memmove.
Der Pointer `r->src` zeigt nach Init auf `buf`. Da `r->src` nach Refill immer
auf `buf` zurückzeigt, kann `kv_reader_refill` den Puffer via
`(uint8_t *)r->src` rekonstruieren — kein separater Puffer-Pointer im Ctx nötig.
Alternativ: `ctx->buf_ptr = buf` explizit im KvReaderCtx speichern (klarer).

### fs_file_read_chunk: Sentinel-Stop

`fs_file_stream_read_from` liest bis EOF (mehrere Callbacks). Um genau einen
Block zu lesen: sink_cb gibt `TDB_ERR_NO_DATA` zurück wenn `buf_cap` voll,
Caller behandelt diesen Status als Erfolg. Bytes gelesen = `sink.written`.

### utf8codepoint Sicherheit

`utf8codepoint(s, &cp)` prüft `!*s` (null-byte-check), nicht bounds.
Mit Overlap = 4 Bytes liegt jede multi-byte Sequenz vollständig im Puffer.
Am echten EOF: verbleibende Bytes = letzter Overlap (valide Source-Bytes).
Boundary-Fälle durch Test step-1c-utf8-boundary-test abgedeckt.

### reader_offset Korrektheit

`reader_offset` = `r->global_offset + r->index`.
`global_offset` wird in `kv_reader_refill` um `r->index` erhöht (konsumierte Bytes).
Die Stuck-Detection in `eval_source_buffer_in_current_state` (`pos_before == pos_after`)
funktioniert korrekt: offset wächst stetig über Chunk-Grenzen.

### Embedded-Sources unverändert

`reader_init_with_length(r, rom_ptr, len)` → `refill_fn = NULL`.
Kein Refill, kein Kopieren. Bestehende Tests bleiben grün.
