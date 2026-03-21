(ns test.launcher-main)

(defn -main
  [& args]
  (println
   (str "launcher-main:"
        (count args)
        ":"
        (nth args 0 nil)
        ":"
        (nth args 1 nil)))
  nil)
