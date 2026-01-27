(ns test-defn-minus-private
  (:require [clojure.test :refer :all]
            [tinyclj.fs :as fs]))

(deftest defn-minus-private-basic
  (testing "defn- erzeugt private Funktion"
    (fs/defn- foo [] :ok)
    (is (= :ok (fs/foo))) ; im gleichen Namespace sichtbar
    (is (thrown? Exception (tinyclj.fs/foo))) ; von außen nicht sichtbar (Debug-Build)
    ))
