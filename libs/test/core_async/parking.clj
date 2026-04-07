;; core.async pub/sub smoke test (macOS/test runtime)
;;
;; Verifies:
;; 1) pub/sub routes events by topic
;; 2) unsub removes one topic subscription
;; 3) unsub-all removes all subscriptions deterministically

(require 'clojure.core.async)

(let [assert-eq (fn [expected actual msg]
                  (when (not (= expected actual))
                    (throw (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual))))
      src (clojure.core.async/chan 8)
      p (clojure.core.async/pub src :source)
      audio-out (clojure.core.async/chan 2)
      button-out (clojure.core.async/chan 2)]
  (do
    (clojure.core.async/sub p :audio audio-out)
    (clojure.core.async/sub p :button button-out)

    (clojure.core.async/put! src {:source :audio :track-id :song-a})
    (assert-eq :song-a (:track-id (clojure.core.async/poll! audio-out)) "audio topic receives audio event")
    (assert-eq nil (clojure.core.async/poll! button-out) "button topic stays isolated")

    (clojure.core.async/unsub p :audio audio-out)
    (clojure.core.async/put! src {:source :audio :track-id :song-b})
    (assert-eq nil (clojure.core.async/poll! audio-out) "unsub removes topic delivery")

    (clojure.core.async/sub p :audio audio-out)
    (clojure.core.async/unsub-all p)
    (clojure.core.async/put! src {:source :audio :track-id :song-c})
    (assert-eq nil (clojure.core.async/poll! audio-out) "unsub-all removes all topic delivery")
    (assert-eq nil (clojure.core.async/poll! button-out) "unsub-all removes all button delivery")
    (println "core_async pub/sub: OK")))
