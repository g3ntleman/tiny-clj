(ns tiny-fx.gfx-timeline)

(def timeline-watchers* (atom {}))

(defn- validate-watch
  [id f]
  (when (nil? id)
    (throw "timeline/watch requires id"))
  (when (not (or (nil? f) (fn? f)))
    (throw "timeline/watch expects fn or nil")))

(defn- dispatch-callback!
  [watcher progress]
  (let [cb (:callback watcher)]
    (when cb
      ;; Defer callback to avoid recursive publish-state! stack growth.
      (let [payload {:source :timeline
                     :id (:id watcher)
                     :progress progress}]
        (schedule 0 (fn timeline-watch-deferred-cb [] (cb payload)))
        true))))

(defn- watcher-source-key
  [progress]
  (let [slot-id (:slot-id progress)
        entity-id (:entity-id progress)
        field (:field progress)]
    (if (and (nil? slot-id) (nil? entity-id) (nil? field))
      nil
      [slot-id entity-id field])))

(defn- watcher-with-updated-edge
  [watcher source-key at-end]
  (if source-key
    (assoc watcher
           :last-at-end at-end
           :last-at-end-by-source (assoc (if (map? (:last-at-end-by-source watcher))
                                           (:last-at-end-by-source watcher)
                                           {})
                                         source-key
                                         at-end))
    (assoc watcher :last-at-end at-end)))

(defn- rearm-source-edges
  [source-edges]
  (if (map? source-edges)
    (loop [entries (seq source-edges)
           result {}]
      (if entries
        (let [[source-key _] (first entries)]
          (recur (next entries) (assoc result source-key true)))
        result))
    {}))

(defn dispatch-watch!
  "Pushes one timeline progress sample into a watcher.

  Emits the callback on a false->true :at-end edge.
  Returns true when a watcher with watch-id exists."
  [watch-id progress]
  (let [watcher (get @timeline-watchers* watch-id)]
    (if (nil? watcher)
      false
      (let [flagged (= true (:end-event progress))
            at-end (and flagged (= true (:at-end progress)))
            source-key (watcher-source-key progress)
            was-at-end (if source-key
                         (= true (get (:last-at-end-by-source watcher) source-key))
                         (= true (:last-at-end watcher)))]
        (when (not= was-at-end at-end)
          (swap! timeline-watchers*
                 (fn [current]
                   (if (contains? current watch-id)
                     (assoc current
                            watch-id
                            (watcher-with-updated-edge (get current watch-id) source-key at-end))
                     current))))
        (when (and at-end (not was-at-end))
          (dispatch-callback! watcher progress))
        true))))

(defn dispatch-progress!
  "Dispatches one renderer timeline progress sample by its embedded :event-id.

Returns true when a watcher for :event-id exists, else false."
  [progress]
  (let [event-id (if (map? progress) (:event-id progress) nil)]
    (if event-id
      (dispatch-watch! event-id progress)
      false)))

(defn reset-watch-edge!
  "Re-arms one watcher edge by priming :last-at-end to true.
This intentionally suppresses one stale :at-end=true sample from the previous
segment; the watcher re-arms itself once it sees :at-end=false on the new
segment and then emits the next real false->true end edge."
  [watch-id]
  (when watch-id
    (swap! timeline-watchers*
           (fn [watchers]
             (if (contains? watchers watch-id)
               (let [watcher (get watchers watch-id)]
                 (assoc watchers
                        watch-id
                        (assoc watcher
                               :last-at-end true
                               :last-at-end-by-source (rearm-source-edges (:last-at-end-by-source watcher)))))
               watchers))))
  nil)

(defn watch
  "Registers or removes one timeline end watcher.

  (watch :ball-end f)
  (watch :ball-end nil)

  The callback receives {:source :timeline :id ... :progress ...}."
  [& args]
  (let [argc (count args)]
    (if (not= argc 2)
      (throw (str "timeline/watch expects 2 arguments, got " argc))
      (let [id (nth args 0)
            f (nth args 1)]
        (validate-watch id f)
        (swap! timeline-watchers*
               (fn [watchers]
                 (if (nil? f)
                   (dissoc watchers id)
                   (assoc watchers
                          id
                          {:id id
                           :callback f
                           :last-at-end false
                           :last-at-end-by-source {}}))))
        nil))))
