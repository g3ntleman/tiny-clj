;; Minimal core.async subset for tiny-clj (macOS first, ESP32 later).
;; IMPORTANT:
;; - API constraint: for every var/macro included here, keep core.async name + arities 1:1.
;; - Unsupported features must throw (never silently no-op).
;;
;; Note: tiny-clj's namespace/require semantics are intentionally minimal.
;; Avoid relying on `(:require ...)` aliasing for correctness; keep forms simple
;; and use core ops unqualified where possible.

(ns clojure.core.async)

;; -----------------------------------------------------------------------------
;; Errors / Unsupported
;; -----------------------------------------------------------------------------

(defn unsupported! [& args]
  (let [feature (first args)
        detail (first (rest args))]
    (if (nil? detail)
      (throw (str "core.async/unsupported: " feature))
      (throw (str "core.async/unsupported: " feature " " detail)))))

(defn illegal-arg! [msg]
  (throw (str "core.async/illegal-arg: " msg)))

;; -----------------------------------------------------------------------------
;; Buffers (API: buffer, sliding-buffer, dropping-buffer)
;; -----------------------------------------------------------------------------

(defn buffer [n]
  (when (or (nil? n) (not (integer? n)) (< n 0))
    (illegal-arg! (str "buffer n must be integer >= 0, got " n)))
  {:tinyclj.buffer/type :fixed
   :tinyclj.buffer/n n})

(defn sliding-buffer [n]
  (when (or (nil? n) (not (integer? n)) (< n 0))
    (illegal-arg! (str "sliding-buffer n must be integer >= 0, got " n)))
  {:tinyclj.buffer/type :sliding
   :tinyclj.buffer/n n})

(defn dropping-buffer [n]
  (when (or (nil? n) (not (integer? n)) (< n 0))
    (illegal-arg! (str "dropping-buffer n must be integer >= 0, got " n)))
  {:tinyclj.buffer/type :dropping
   :tinyclj.buffer/n n})

;; -----------------------------------------------------------------------------
;; Channels (subset step 1)
;;
;; Represented as an Atom holding a mutable state map:
;;   {:items [...] :head 0 :cap N :buf-type :fixed|:sliding|:dropping :closed false
;;    ;; Step 2 (callbacks):
;;    :takes [...] :takes-head 0
;;    :puts  [...] :puts-head  0
;;    :deliveries [...]}
;;
;; Notes:
;; - nil values are not allowed (core.async rule).
;; - This is macOS-first; ESP32 backend can later replace the internals.
;; -----------------------------------------------------------------------------

(defn parse-buf-or-n [buf-or-n]
  (if (nil? buf-or-n)
    {:buf-type :fixed :cap 0}
    (if (integer? buf-or-n)
      (do
        (if (< buf-or-n 0)
          (illegal-arg! (str "chan buffer size must be >= 0, got " buf-or-n))
          nil)
        {:buf-type :fixed :cap buf-or-n})
      (if (and (map? buf-or-n)
               (contains? buf-or-n :tinyclj.buffer/type)
               (contains? buf-or-n :tinyclj.buffer/n))
        (let [t (get buf-or-n :tinyclj.buffer/type)
              n (get buf-or-n :tinyclj.buffer/n)]
          (do
            (if (or (nil? n) (not (integer? n)) (< n 0))
              (illegal-arg! (str "chan buffer n must be integer >= 0, got " n))
              nil)
            (if (not (or (= t :fixed) (= t :sliding) (= t :dropping)))
              (illegal-arg! (str "unknown buffer type " t))
              nil)
            {:buf-type t :cap n}))
        (illegal-arg! (str "unsupported buffer argument to chan: " buf-or-n))))))

(defn chan [& args]
  ;; core.async arities: (chan), (chan buf-or-n), (chan buf-or-n xform), (chan buf-or-n xform ex-handler)
  (let [argc (count args)]
    (if (> argc 3)
      (illegal-arg! (str "chan arity " argc " not supported"))
      (let [buf-or-n (if (>= argc 1) (first args) nil)
            xform (if (>= argc 2) (first (rest args)) nil)
            ex-handler (if (>= argc 3) (first (rest (rest args))) nil)]
        ;; IMPORTANT: tiny-clj currently does not reliably treat multi-form function bodies
        ;; as an implicit (do ...). So we always use explicit (do ...) when sequencing.
        (do
          (if (nil? xform) nil (unsupported! "chan" "xform"))
          (if (nil? ex-handler) nil (unsupported! "chan" "ex-handler"))
          (let [spec (parse-buf-or-n buf-or-n)
                cap (get spec :cap)
                buf-type (get spec :buf-type)
                st0 {}
                st1 (assoc st0 :items (vector))
                st2 (assoc st1 :head 0)
                st3 (assoc st2 :cap cap)
                st4 (assoc st3 :buf-type buf-type)
                st5 (assoc st4 :closed false)
                ;; Step 2 queues + delivery queue
                st6 (assoc st5 :takes (vector))
                st7 (assoc st6 :takes-head 0)
                st8 (assoc st7 :puts (vector))
                st9 (assoc st8 :puts-head 0)
                st10 (assoc st9 :deliveries (vector))
                st st10]
            (atom st)))))))

(defn compact-queue [st items-k head-k]
  (let [h (get st head-k)
        items (get st items-k)]
    (if (> h 32)
      (let [s1 (assoc st items-k (vec (drop h items)))
            s2 (assoc s1 head-k 0)]
        s2)
      st)))

(defn q-size [st items-k head-k]
  (let [items (get st items-k)
        h (get st head-k)]
    (- (count items) h)))

(defn q-peek [st items-k head-k]
  (let [items (get st items-k)
        h (get st head-k)
        sz (- (count items) h)]
    (if (<= sz 0)
      nil
      (nth items h))))

(defn q-pop [st items-k head-k]
  (let [v (q-peek st items-k head-k)]
    (if (nil? v)
      (vector nil st)
      (let [h (get st head-k)
            s1 (assoc st head-k (+ h 1))
            s2 (compact-queue s1 items-k head-k)]
        (vector v s2)))))

(defn q-clear [st items-k head-k]
  (let [s1 (assoc st items-k (vector))
        s2 (assoc s1 head-k 0)]
    s2))

(defn q-push [st items-k x]
  (assoc st items-k (conj (get st items-k) x)))

(defn add-delivery [st f arg]
  ;; IMPORTANT: Never call nil as a function; keep deliveries explicit.
  (if (nil? f)
    st
    (assoc st :deliveries (conj (get st :deliveries) (vector f arg)))))

(defn run-deliveries! [ch]
  (let [captured (atom (vector))]
    (do
      (swap! ch (fn [st]
                  (do
                    (reset! captured (get st :deliveries))
                    (assoc st :deliveries (vector)))))
      (let [ds (deref captured)
            n (count ds)
            sched (fn sched [i]
                    (if (< i n)
                      (let [entry (nth ds i)
                            f (nth entry 0)
                            arg (nth entry 1)]
                        (do
                          ;; Use event loop for fairness.
                          ;; Closure-capture works in tiny-clj now, so we can schedule directly.
                          (clojure.core/schedule 0 (fn [] (f arg)))
                          (sched (+ i 1))))
                      nil))]
        (sched 0)))))

(defn buf-size [st]
  (- (count (get st :items)) (get st :head)))

(defn buf-pop [st]
  (let [head (get st :head)
        items (get st :items)
        sz (- (count items) head)]
    (if (<= sz 0)
      (vector nil st)
      (let [v (nth items head)
            s1 (compact-state (assoc st :head (+ head 1)))]
        (vector v s1)))))

(defn buf-append [st val]
  (assoc (compact-state st) :items (conj (get st :items) val)))

(defn drain-step [s]
  ;; Single step + tail recursion (no `loop` special form).
  (let [takers (q-size s :takes :takes-head)
        putters (q-size s :puts :puts-head)
        cap (get s :cap)
        t (get s :buf-type)
        bsz (buf-size s)]
    (if (> takers 0)
      (if (> bsz 0)
        ;; take from buffer
        (let [th (q-peek s :takes :takes-head)
              popped (buf-pop s)
              v (nth popped 0)
              s1 (nth popped 1)
              s2 (nth (q-pop s1 :takes :takes-head) 1)
              s3 (add-delivery s2 th v)]
          (drain-step s3))
        (if (> putters 0)
          ;; handoff put->take (unbuffered or empty buffer)
          (let [th (q-peek s :takes :takes-head)
                popped-put (q-pop s :puts :puts-head)
                put-entry (nth popped-put 0)
                s1 (nth popped-put 1)
                v (nth put-entry 0)
                ph (nth put-entry 1)
                s2 (nth (q-pop s1 :takes :takes-head) 1)
                s3 (add-delivery (add-delivery s2 th v) ph true)]
            (drain-step s3))
          s))
      ;; no takers
      (if (> putters 0)
        (if (> cap 0)
          (if (< bsz cap)
            ;; move a pending put into buffer
            (let [popped-put (q-pop s :puts :puts-head)
                  put-entry (nth popped-put 0)
                  s1 (nth popped-put 1)
                  v (nth put-entry 0)
                  ph (nth put-entry 1)
                  s2 (buf-append s1 v)
                  s3 (add-delivery s2 ph true)]
              (drain-step s3))
            (if (= t :sliding)
              ;; sliding accepts even when full
              (let [popped-put (q-pop s :puts :puts-head)
                    put-entry (nth popped-put 0)
                    s1 (nth popped-put 1)
                    v (nth put-entry 0)
                    ph (nth put-entry 1)
                    h (get s1 :head)
                    s2 (buf-append (compact-state (assoc s1 :head (+ h 1))) v)
                    s3 (add-delivery s2 ph true)]
                (drain-step s3))
              (if (= t :dropping)
                ;; dropping completes but discards value
                (let [popped-put (q-pop s :puts :puts-head)
                      put-entry (nth popped-put 0)
                      s1 (nth popped-put 1)
                      ph (nth put-entry 1)
                      s2 (add-delivery s1 ph true)]
                  (drain-step s2))
                ;; fixed full: keep pending puts parked
                s)))
          ;; cap == 0 (unbuffered): need a taker, otherwise keep parked puts
          s)
        s))))

(defn drain [st]
  (drain-step st))

(defn close! [ch]
  ;; Mark closed and flush pending takes/puts deterministically.
  (do
    (swap! ch
           (fn [st]
             (if (get st :closed)
               st
               (let [s1 (assoc st :closed true)
                     ;; fail all pending puts
                     puts-items (get s1 :puts)
                     puts-head (get s1 :puts-head)
                     puts-n (count puts-items)
                     s2 (let [flush-puts (fn flush-puts [i s]
                                           (if (< i puts-n)
                                             (let [entry (nth puts-items i)
                                                   ph (nth entry 1)]
                                               (flush-puts (+ i 1) (add-delivery s ph false)))
                                             s))]
                          (flush-puts puts-head s1))
                     s3 (q-clear s2 :puts :puts-head)
                     ;; drain buffered items to waiting takes (but do not accept puts anymore)
                     s4 (drain s3)
                     ;; remaining takers get nil
                     takes-items (get s4 :takes)
                     takes-head (get s4 :takes-head)
                     takes-n (count takes-items)
                     s5 (let [flush-takes (fn flush-takes [i s]
                                            (if (< i takes-n)
                                              (let [th (nth takes-items i)]
                                                (flush-takes (+ i 1) (add-delivery s th nil)))
                                              s))]
                          (flush-takes takes-head s4))
                     s6 (q-clear s5 :takes :takes-head)]
                 s6))))
    (run-deliveries! ch)
    nil))

(defn closed? [ch]
  (get (deref ch) :closed))

(defn compact-state [st]
  ;; Keep memory bounded: if head grows large, drop consumed prefix.
  (let [head (get st :head)
        items (get st :items)]
    ;; Simple heuristic (avoid relying on division helpers).
    (if (> head 32)
      (let [s1 (assoc st :items (vec (drop head items)))
            s2 (assoc s1 :head 0)]
        s2)
      st)))

(defn offer! [ch val]
  (do
    (if (nil? val)
      (illegal-arg! "nil is not a valid value for core.async channels")
      nil)
    (let [ret (atom false)]
      (do
        (swap! ch
               (fn [st]
                 (if (get st :closed)
                   (do
                     (reset! ret false)
                     st)
                   (let [takers (q-size st :takes :takes-head)]
                     (if (> takers 0)
                       ;; handoff directly to waiting taker
                       (let [th (q-peek st :takes :takes-head)
                             s1 (nth (q-pop st :takes :takes-head) 1)
                             s2 (add-delivery s1 th val)]
                         (do
                           (reset! ret true)
                           s2))
                       (let [cap (get st :cap)
                             t (get st :buf-type)
                             bsz (buf-size st)]
                         (if (> cap 0)
                           (if (< bsz cap)
                             (do
                               (reset! ret true)
                               (buf-append st val))
                             (if (= t :sliding)
                               (do
                                 (reset! ret true)
                                 (let [h (get st :head)]
                                   (buf-append (compact-state (assoc st :head (+ h 1))) val)))
                               (if (= t :dropping)
                                 (do
                                   (reset! ret true)
                                   st)
                                 (do
                                   (reset! ret false)
                                   st))))
                           (do
                             ;; unbuffered, no taker: offer! fails
                             (reset! ret false)
                             st))))))))
        (run-deliveries! ch)
        (deref ret)))))

(defn poll! [ch]
  (let [ret (atom nil)]
    (do
      (swap! ch
             (fn [st]
               (let [bsz (buf-size st)]
                 (if (> bsz 0)
                   (let [p (buf-pop st)
                         v (nth p 0)
                         s1 (nth p 1)
                         s2 (drain s1)]
                     (do
                       (reset! ret v)
                       s2))
                   (let [putters (q-size st :puts :puts-head)]
                     (if (> putters 0)
                       ;; unbuffered value available via pending put
                       (let [popped (q-pop st :puts :puts-head)
                             put-entry (nth popped 0)
                             s1 (nth popped 1)
                             v (nth put-entry 0)
                             ph (nth put-entry 1)
                             s2 (add-delivery s1 ph true)]
                         (do
                           (reset! ret v)
                           s2))
                       (do
                         (reset! ret nil)
                         st)))))))
      (run-deliveries! ch)
      (deref ret))))

;; -----------------------------------------------------------------------------
;; Stubs for later steps (must exist, must throw)
;; -----------------------------------------------------------------------------

(defn put-impl [port val fn1 on-caller?]
  (do
    (if (nil? val)
      (illegal-arg! "nil is not a valid value for core.async channels")
      nil)
    (if (nil? on-caller?)
      nil
      (if (= on-caller? false)
        (unsupported! "put!" "on-caller?")
        nil))
    ;; Unsupported policy (Step 2):
    ;; - This is single-threaded and runs callbacks immediately on the caller.
    ;; - `on-caller? = false` is currently unsupported and throws with `core.async/unsupported:`.
    (let [ch port
          ret (atom true)]
      (do
        (swap! ch
               (fn [st]
                 (if (get st :closed)
                   (do
                     (reset! ret false)
                     (add-delivery st fn1 false))
                   (let [s1 (q-push st :puts (vector val fn1))
                         s2 (drain s1)]
                     (do
                       (reset! ret true)
                       s2)))))
        (run-deliveries! ch)
        (deref ret)))))

(defn put! [& args]
  ;; core.async arities: (put! port val), (put! port val fn1), (put! port val fn1 on-caller?)
  (let [argc (count args)]
    (if (= argc 2)
      (let [port (first args)
            val (nth args 1)]
        (put-impl port val nil true))
      (if (= argc 3)
        (let [port (first args)
              val (nth args 1)
              fn1 (nth args 2)]
          (put-impl port val fn1 true))
        (if (= argc 4)
          (let [port (first args)
                val (nth args 1)
                fn1 (nth args 2)
                on-caller? (nth args 3)]
            (put-impl port val fn1 on-caller?))
          (illegal-arg! (str "put! arity " argc " not supported")))))))

(defn take-impl [port fn1 on-caller?]
  (do
    (if (nil? fn1)
      (illegal-arg! "take! requires a callback fn1")
      nil)
    (if (nil? on-caller?)
      nil
      (if (= on-caller? false)
        (unsupported! "take!" "on-caller?")
        nil))
    ;; Unsupported policy (Step 2):
    ;; - This is single-threaded and runs callbacks immediately on the caller.
    ;; - `on-caller? = false` is currently unsupported and throws with `core.async/unsupported:`.
    (let [ch port]
      (do
        (swap! ch
               (fn [st]
                 (let [bsz (buf-size st)]
                   (if (and (get st :closed) (<= bsz 0))
                     ;; closed and empty: immediate nil
                     (add-delivery st fn1 nil)
                     (let [s1 (q-push st :takes fn1)
                           s2 (drain s1)]
                       s2)))))
        (run-deliveries! ch)
        nil))))

(defn take! [& args]
  ;; core.async arities: (take! port fn1), (take! port fn1 on-caller?)
  (let [argc (count args)]
    (if (= argc 2)
      (let [port (first args)
            fn1 (nth args 1)]
        (take-impl port fn1 true))
      (if (= argc 3)
        (let [port (first args)
              fn1 (nth args 1)
              on-caller? (nth args 2)]
          (take-impl port fn1 on-caller?))
        (illegal-arg! (str "take! arity " argc " not supported"))))))

(defn timeout [msecs]
  (unsupported! "timeout"))

(defmacro go [& body]
  (list 'clojure.core.async/unsupported! "go"))

(defmacro go-loop [bindings & body]
  (list 'clojure.core.async/unsupported! "go-loop"))

(defmacro <! [port]
  (list 'clojure.core.async/unsupported! "<!"))

(defmacro >! [port val]
  (list 'clojure.core.async/unsupported! ">!"))

(defmacro alts! [ports & opts]
  (list 'clojure.core.async/unsupported! "alts!"))

