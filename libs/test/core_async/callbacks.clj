;; Step 2 tests: put!/take! callbacks + parking semantics.
;;
;; Run:
;;   ./build/tiny-clj-repl -f libs/test/core_async/callbacks.clj
;;
;; NOTE: Callbacks must not close over `let` locals in tiny-clj yet, so these tests
;; use global `def` atoms for capturing results.
;;
;; Each test is a self-contained top-level form.

(do
  (require 'clojure.core.async)
  (def out (atom (vector)))
  (defn drain-events! []
    (if (run-next-task)
      (drain-events!)
      nil))
  (defn push! [x]
    (do
      (swap! out (fn [s] (conj s x)))
      nil))
  (defn reset!out []
    (do
      (reset! out (vector))
      nil))
  (let [eq? (fn [expected actual]
              (if (nil? expected)
                (nil? actual)
                (if (nil? actual) false (= expected actual))))
        assert-eq (fn [expected actual msg]
                    (when (not (eq? expected actual))
                      (throw (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual))))]

    ;; Unbuffered handoff: take! parks, put! delivers.
    (reset!out)
    (let [ch (clojure.core.async/chan 0)]
      (do
        (clojure.core.async/take! ch (fn [v] (push! (vector :take v))))
        (assert-eq (vector) (deref out) "unbuffered: no value yet")
        (assert-eq true (clojure.core.async/put! ch :x (fn [ok] (push! (vector :put ok))))
                   "unbuffered: put! returns true")
        (drain-events!)
        (assert-eq (vector (vector :take :x) (vector :put true))
                   (deref out)
                   "unbuffered: callbacks fired in order")))

    (println "core_async callbacks unbuffered: OK")))

(do
  (require 'clojure.core.async)
  (def out (atom (vector)))
  (defn drain-events! []
    (if (run-next-task)
      (drain-events!)
      nil))
  (defn push! [x]
    (do
      (swap! out (fn [s] (conj s x)))
      nil))
  (defn reset!out []
    (do
      (reset! out (vector))
      nil))
  (let [eq? (fn [expected actual]
              (if (nil? expected)
                (nil? actual)
                (if (nil? actual) false (= expected actual))))
        assert-eq (fn [expected actual msg]
                    (when (not (eq? expected actual))
                      (throw (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual))))]

    ;; Buffered fixed: second put parks until take frees capacity.
    (reset!out)
    (let [ch (clojure.core.async/chan (clojure.core.async/buffer 1))]
      (do
        (assert-eq true (clojure.core.async/put! ch :a (fn [ok] (push! (vector :put-a ok))))
                   "buffered: put a accepted")
        (drain-events!)
        (assert-eq true (clojure.core.async/put! ch :b (fn [ok] (push! (vector :put-b ok))))
                   "buffered: put b parked (returns true)")
        (drain-events!)
        ;; b callback should not have fired yet (buffer full).
        (assert-eq (vector (vector :put-a true))
                   (deref out)
                   "buffered: only put-a callback fired")
        ;; take one: should deliver a, and then b should be committed (put-b callback fires).
        (clojure.core.async/take! ch (fn [v] (push! (vector :take-1 v))))
        (drain-events!)
        (assert-eq (vector (vector :put-a true)
                           (vector :take-1 :a)
                           (vector :put-b true))
                   (deref out)
                   "buffered: take frees capacity -> put-b proceeds")
        ;; take second: should deliver b
        (clojure.core.async/take! ch (fn [v] (push! (vector :take-2 v))))
        (drain-events!)
        (assert-eq (vector (vector :put-a true)
                           (vector :take-1 :a)
                           (vector :put-b true)
                           (vector :take-2 :b))
                   (deref out)
                   "buffered: second take gets b")))

    (println "core_async callbacks buffered: OK")))

(do
  (require 'clojure.core.async)
  (def out (atom (vector)))
  (defn drain-events! []
    (if (run-next-task)
      (drain-events!)
      nil))
  (defn push! [x]
    (do
      (swap! out (fn [s] (conj s x)))
      nil))
  (defn reset!out []
    (do
      (reset! out (vector))
      nil))
  (let [eq? (fn [expected actual]
              (if (nil? expected)
                (nil? actual)
                (if (nil? actual) false (= expected actual))))
        assert-eq (fn [expected actual msg]
                    (when (not (eq? expected actual))
                      (throw (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual))))
        assert-true (fn [x msg]
                      (when (not x)
                        (throw (str "ASSERT FAIL: " msg))))
        throws? (fn [thunk]
                  (try
                    (do (thunk) false)
                    (catch e true)))]

    ;; close! flushes parked take with nil
    (reset!out)
    (let [ch (clojure.core.async/chan 0)]
      (do
        (clojure.core.async/take! ch (fn [v] (push! (vector :take v))))
        (clojure.core.async/close! ch)
        (drain-events!)
        (assert-eq (vector (vector :take nil)) (deref out) "close flushes taker with nil")))

    ;; close! flushes parked put with false
    (reset!out)
    (let [ch (clojure.core.async/chan 0)]
      (do
        (assert-eq true (clojure.core.async/put! ch :x (fn [ok] (push! (vector :put ok))))
                   "put returns true (parked)")
        (clojure.core.async/close! ch)
        (drain-events!)
        (assert-eq (vector (vector :put false)) (deref out) "close flushes put with false")))

    ;; unsupported: on-caller? = false
    (assert-true (throws? (fn [] (clojure.core.async/put! (clojure.core.async/chan 0) :x (fn [ok] ok) false)))
                 "put! on-caller? false throws")
    (assert-true (throws? (fn [] (clojure.core.async/take! (clojure.core.async/chan 0) (fn [v] v) false)))
                 "take! on-caller? false throws")

    (println "core_async callbacks close+unsupported: OK")))

(println "core_async callbacks: OK")

