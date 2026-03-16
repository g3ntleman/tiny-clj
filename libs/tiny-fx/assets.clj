(ns tiny-fx.assets)

(def cache* (atom {}))

(defn reset-cache!
  "Resets the EDN asset cache."
  []
  (reset! cache* {}))

(defn load-edn-asset
  "Loads an EDN asset from path. If required-keys is provided, validates that all keys are present.
  Uses a cache to only parse the file once."
  [path required-keys]
  (if-let [cached (get @cache* path)]
    cached
    (let [s (or (slurp path) (throw (str "Asset not found: " path)))
          data (read-string s)]
      (when required-keys
        (doseq [k required-keys]
          (if (not (contains? data k))
            (throw (str "Asset " path " missing required key: " k)))))
      (swap! cache* assoc path data)
      data)))
