(ns tiny-fx.assets)

(defn- parse-edn-asset
  [path required-keys]
  (let [s (or (slurp path) (throw (str "Asset not found: " path)))
        data (read-string s)]
    (when required-keys
      (doseq [k required-keys]
        (if (not (contains? data k))
          (throw (str "Asset " path " missing required key: " k)))))
    data))

(defn edn-asset
  "Loads and parses an EDN asset from path.
  tiny-fx.assets never caches; callers can cache explicitly if needed."
  [path required-keys]
  (parse-edn-asset path required-keys))

(defn edn-asset-under-prefix
  "Loads an EDN asset under /assets/<ns-path>/<file-name>."
  [ns-path file-name required-keys]
  (edn-asset (str "/assets/" ns-path "/" file-name) required-keys))
