;; Transient Vector — Ringbuffer-Semantik (intern, seit Phase 4)
;;
;; Transiente Vektoren nutzen intern einen Ringbuffer mit einem `head`-Offset.
;; Das erlaubt O(1)-Entfernung vom Anfang ohne Array-Verschiebung.
;;
;; ============================================================
;; Öffentliche API — unverändert
;; ============================================================
;;
;; (def tv (transient [1 2 3 4 5]))
;;
;; Hinten anfügen:
;;   (conj! tv 6)                  ; O(1)
;;
;; Hinten entfernen:
;;   (pop! tv)                     ; O(1)
;;
;; Queue-Nutzung (FIFO):
;;   ;; Aufgaben einreihen
;;   (conj! tv :task-a)
;;   (conj! tv :task-b)
;;   (conj! tv :task-c)
;;
;;   ;; Nächste Aufgabe holen und entfernen — O(1) dank Ringbuffer
;;   ;; (intern: vector_remove_at(tvec, 0) inkrementiert nur head)
;;
;;   ;; Snapshot für read-only Zugriff:
;;   (persistent! tv)   ; gibt logisch geordneten persistenten Vektor zurück,
;;                      ; unabhängig vom internen head-Offset
;;
;; ============================================================
;; Interne Mechanik
;; ============================================================
;;
;; - `head` zeigt auf das erste logische Element im backing-Array.
;; - Physischer Index: (head + logischer-Index) mod capacity
;; - Komplexitäten:
;;     conj! / vector_push         O(1) — schreibt an (head+count) mod cap
;;     pop!  / vector_pop          O(1) — gibt Tail-Slot frei
;;     remove-at 0                 O(1) — inkrementiert head
;;     remove-at N (N > 0)         O(n) — wrap-bewusstes Shiften
;;     insert-at N                 O(n) — wrap-bewusstes Shiften
;;
;; - Wachstums-Invariante: Wenn das Backing wächst, werden die Elemente in
;;   logischer Reihenfolge in den neuen Puffer kopiert und head wird auf 0
;;   zurückgesetzt.  Nach jedem Wachsen gilt: head = 0.
;;
;; - persistent! (vector_persistent):
;;     head == 0  → gibt das Backing direkt zurück (kein Alloc, borrowed)
;;     head != 0  → allokiert eine normalisierte Kopie in logischer Reihenfolge
;;
;; ============================================================
;; ESP32-spezifisch
;; ============================================================
;;
;; Auf ESP32-Targets (ESP_PLATFORM / ESP32_BUILD) sind count und capacity
;; in CljPersistentVector auf uint16_t geschrumpft (je 2 Bytes statt 4).
;; Das spart 4 Bytes Header pro persistentem Vektor.
;; Die maximale Kapazität beträgt auf allen Plattformen 65535 Elemente —
;; auf dem Host identisch, damit Tests die ESP32-Grenzen korrekt abbilden.
;;
;; Beispiel-Headergröße (Host, LP64):
;;   CljObject          4 Bytes (type + flags + rc)
;;   [padding]          4 Bytes (Ausrichtung für 32-bit count)
;;   unsigned int count 4 Bytes
;;   int capacity       4 Bytes
;;   ---------------------------
;;   CljPersistentVector: 16 Bytes (ohne data[])
;;
;; Auf ESP32 (ILP32, uint16_t fields):
;;   CljObject          4 Bytes
;;   uint16_t count     2 Bytes
;;   uint16_t capacity  2 Bytes
;;   ---------------------------
;;   CljPersistentVector: 8 Bytes (ohne data[])  → -8 Bytes pro Vektor
;;
;; Der transiente Wrapper (CljTransientVector) gewinnt ebenfalls:
;;   head ist uint16_t statt unsigned int → -2 Bytes (nach Ausrichtung)
