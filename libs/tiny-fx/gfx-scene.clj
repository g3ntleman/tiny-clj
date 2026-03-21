(ns tiny-fx.gfx-scene
  (:require [tiny-fx.gfx]))

(defn ->Style [stroke-color stroke-width visible has-fill fill-color has-bg-color bg-color]
  (record-create 'Style [stroke-color stroke-width visible has-fill fill-color has-bg-color bg-color]))

(defn ->Group [id t style visible children anim]
  (record-create 'Group [id t style visible children anim]))

(defn ->Transform [id t style visible x y scale rot children anim]
  (record-create 'Transform [id t style visible x y scale rot children anim]))

(defn ->Line [id t style visible x1 y1 x2 y2 anim]
  (record-create 'Line [id t style visible x1 y1 x2 y2 anim]))

(defn ->Rect [id t style visible x y w h anim]
  (record-create 'Rect [id t style visible x y w h anim]))

(defn ->Polyline [id t style visible pts closed anim]
  (record-create 'Polyline [id t style visible pts closed anim]))

(defn ->VText [id t style visible x y scale rot text anim]
  (record-create 'VText [id t style visible x y scale rot text anim]))

(defn edn->scene
  "Recursively converts generic EDN maps with a :type keyword into corresponding gfx-scene records."
  [m]
  (cond
    (map? m)
    (let [m2 (reduce (fn [acc [k v]] (assoc acc k (edn->scene v))) {} m)
          t (:type m2)]
      (if t
        (record-from-map (symbol (name t)) (dissoc m2 :type))
        m2))
    (vector? m)
    (mapv edn->scene m)
    :else m))

;; Collision/spatial contract helpers.
;; CollisionRule remains a legacy schema bridge.
;; SpatialRule/SpatialEvent define the active host-side trigger contract.

(defn- phase-present?
  [xs v]
  (loop [rest-xs xs]
    (if (empty? rest-xs)
      false
      (if (= (first rest-xs) v)
        true
        (recur (rest rest-xs))))))

(defn normalize-collision-phase-mask
  "Normalizes phase mask for collision rules.
Accepts vector/list/keyword/nil and returns a validated vector."
  [phase-mask]
  (let [raw (cond
              (nil? phase-mask) [:enter :exit]
              (vector? phase-mask) phase-mask
              (list? phase-mask) (vec phase-mask)
              (keyword? phase-mask) [phase-mask]
              :else [])
        normalized (let [out []
                         out (if (phase-present? raw :enter) (conj out :enter) out)
                         out (if (phase-present? raw :exit) (conj out :exit) out)]
                     out)]
    (if (empty? normalized)
      [:enter :exit]
      normalized)))

(defn normalize-collision-rule
  "Applies legacy collision-rule defaults and normalization."
  [rule]
  {:id (get rule :id)
   :slot (or (get rule :slot) :game)
   :a-id (get rule :a-id)
   :b-id (get rule :b-id)
   :phase-mask (normalize-collision-phase-mask (get rule :phase-mask))
   :enabled (if (nil? (get rule :enabled)) true (not= (get rule :enabled) false))
   :cooldown-ms (let [v (get rule :cooldown-ms)]
                  (if (nil? v) 0 v))})

(defn normalize-spatial-rule
  "Applies spatial-rule defaults for `:collision` / `:proximity` trigger wiring.
The runtime emits only edge transitions (`:enter` / `:exit`)."
  [rule]
  {:id (get rule :id)
   :slot (or (get rule :slot) :game)
   :kind (or (get rule :kind) :collision)
   :self (let [v (get rule :self)]
           (if (nil? v) (get rule :a-id) v))
   :other (let [v (get rule :other)]
            (if (nil? v) (get rule :b-id) v))
   :radius (let [v (get rule :radius)]
             (if (nil? v) 0 v))
   :channel (get rule :channel)})

(defn update-nodes
  "Batched scene-tree update. Takes a root node and a map of
   {:id update-fn ...}. Performs a single recursive walk, applying
   each update-fn to the node with matching :id. Once all updates
   have been applied, remaining subtrees are returned unchanged."
  [node updates]
  (if (empty? updates)
    node
    (let [id       (:id node)
          upd-fn   (when id (get updates id))
          node2    (if upd-fn (upd-fn node) node)
          updates2 (if upd-fn (dissoc updates id) updates)
          children (:children node2)]
      (if children
        (assoc node2 :children
          (mapv (fn [ch] (update-nodes ch updates2)) children))
        node2))))
