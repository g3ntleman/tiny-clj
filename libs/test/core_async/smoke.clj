;; High-level core.async subset smoke tests (run under macOS tiny-clj-repl).
;;
;; Run:
;;   ./build/tiny-clj-repl -f libs/test/core_async/smoke.clj
;;
;; NOTE: tiny-clj has stricter/earlier symbol resolution than JVM Clojure.
;; Each test is a self-contained top-level form that:
;; - requires `clojure.core.async` inside the form
;; - uses fully-qualified symbols (no `:as` aliases)
;; - uses explicit `(do ...)` whenever sequencing is needed

(do
  (require 'clojure.core.async)
  (let [eq? (fn [expected actual]
              (if (nil? expected)
                (nil? actual)
                (if (nil? actual) false (= expected actual))))
        assert-true (fn [x msg] (when (not x) (throw (str "ASSERT FAIL: " msg))))
        assert-eq (fn [expected actual msg]
                    (when (not (eq? expected actual))
                      (throw (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual))))]
    (let [ch (clojure.core.async/chan (clojure.core.async/buffer 2))]
      (do
        (assert-true (not (clojure.core.async/closed? ch)) "new chan is open")
        (assert-eq true (clojure.core.async/offer! ch :a) "offer! first fits")
        (assert-eq true (clojure.core.async/offer! ch :b) "offer! second fits")
        (assert-eq false (clojure.core.async/offer! ch :c) "offer! over capacity fails (fixed buffer)")
        (assert-eq :a (clojure.core.async/poll! ch) "poll! yields first")
        (assert-eq :b (clojure.core.async/poll! ch) "poll! yields second")
        (assert-eq nil (clojure.core.async/poll! ch) "poll! empty yields nil")
        (clojure.core.async/close! ch)
        (assert-true (clojure.core.async/closed? ch) "close! marks closed")
        (assert-eq false (clojure.core.async/offer! ch :x) "offer! on closed returns false")))
    (println "core_async smoke step1 fixed: OK")))

(do
  (require 'clojure.core.async)
  (let [eq? (fn [expected actual]
              (if (nil? expected)
                (nil? actual)
                (if (nil? actual) false (= expected actual))))
        assert-eq (fn [expected actual msg]
                    (when (not (eq? expected actual))
                      (throw (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual))))]
    (let [ch (clojure.core.async/chan (clojure.core.async/sliding-buffer 2))]
      (do
        (assert-eq true (clojure.core.async/offer! ch :a) "sliding offer! a")
        (assert-eq true (clojure.core.async/offer! ch :b) "sliding offer! b")
        (assert-eq true (clojure.core.async/offer! ch :c) "sliding offer! c drops oldest")
        (assert-eq :b (clojure.core.async/poll! ch) "sliding drops oldest (a), yields b")
        (assert-eq :c (clojure.core.async/poll! ch) "sliding yields c")))
    (println "core_async smoke step1 sliding: OK")))

(do
  (require 'clojure.core.async)
  (let [eq? (fn [expected actual]
              (if (nil? expected)
                (nil? actual)
                (if (nil? actual) false (= expected actual))))
        assert-eq (fn [expected actual msg]
                    (when (not (eq? expected actual))
                      (throw (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual))))]
    (let [ch (clojure.core.async/chan (clojure.core.async/dropping-buffer 2))]
      (do
        (assert-eq true (clojure.core.async/offer! ch :a) "dropping offer! a")
        (assert-eq true (clojure.core.async/offer! ch :b) "dropping offer! b")
        (assert-eq true (clojure.core.async/offer! ch :c) "dropping offer! completes but drops when full")
        (assert-eq :a (clojure.core.async/poll! ch) "dropping yields a")
        (assert-eq :b (clojure.core.async/poll! ch) "dropping yields b")
        (assert-eq nil (clojure.core.async/poll! ch) "dropping empty yields nil")))
    (println "core_async smoke step1 dropping: OK")))

(println "core_async smoke: OK")

