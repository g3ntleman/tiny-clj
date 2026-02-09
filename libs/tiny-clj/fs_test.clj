(ns tiny-clj.fs-test
  (:require [clojure.test :refer :all]
            [tiny-clj.fs :as fs]
            [tiny-db.kv :as kv]))

(deftest meta-set-and-listing
  (let [test-path "/testfile"
        meta {:foo "bar" :x 42}
        _ (fs/spit-bytes test-path (.getBytes "abc" "UTF-8"))
        _ (fs/meta-set! test-path meta)
        entries (fs/list "/")
        entry (first (filter #(= (:path %) test-path) entries))]
    (is (= (:foo (:meta entry)) "bar"))
    (is (= (:x (:meta entry)) 42))))

(deftest meta-set-empty
  (let [test-path "/testfile2"
        _ (fs/spit-bytes test-path (.getBytes "abc" "UTF-8"))
        _ (fs/meta-set! test-path {})
        entries (fs/list "/")
        entry (first (filter #(= (:path %) test-path) entries))]
    (is (map? (:meta entry)))
    (is (empty? (dissoc (:meta entry) :size :chunks)))))

(deftest meta-overwrite
  (let [test-path "/testfile3"
        _ (fs/spit-bytes test-path (.getBytes "abc" "UTF-8"))
        _ (fs/meta-set! test-path {:foo 1})
        _ (fs/meta-set! test-path {:foo 2 :bar 3})
        entries (fs/list "/")
        entry (first (filter #(= (:path %) test-path) entries))]
    (is (= (:foo (:meta entry)) 2))
    (is (= (:bar (:meta entry)) 3))))
