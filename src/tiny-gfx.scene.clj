R"TINY_GFX_SCENE(
(ns tiny-gfx.scene)

;; Central scene-record schema for tiny-gfx/tiny-clj vector rendering.
;;
;; defrecord registers each type with the C renderer (which matches record
;; type names by exact unqualified symbol cname) and creates ->Type /
;; map->Type constructors that other namespaces can import via :refer.

(defrecord Transform [tx ty sx sy rot])
(defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color])
(defrecord Group [id t style visible children])
(defrecord Line [id t style visible x1 y1 x2 y2])
(defrecord Polyline [id t style visible pts closed])
(defrecord Rect [id t style visible x y w h])
(defrecord Tri [id t style visible x1 y1 x2 y2 x3 y3])
(defrecord VText [id t style visible x y scale rot text])
(defrecord Scene [root clip-rect erase-color collision-rules])
(defrecord FrameScene [root clip-rect z visible opaque erase-color guard-px collision-rules])
(defrecord CollisionRule [id slot a-id b-id phase-mask enabled cooldown-ms])
(defrecord CollisionEvent [rule-id slot a-id b-id phase snapshot-gen ts-ms])

;; Collision contract helpers (Step 1: contract freeze, no runtime wiring yet).
(def default-collision-slot :game)
(def default-collision-phase-mask [:enter :exit])

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
              (nil? phase-mask) default-collision-phase-mask
              (vector? phase-mask) phase-mask
              (list? phase-mask) (vec phase-mask)
              (keyword? phase-mask) [phase-mask]
              :else [])
        normalized (let [out [] 
                         out (if (phase-present? raw :enter) (conj out :enter) out)
                         out (if (phase-present? raw :stay) (conj out :stay) out)
                         out (if (phase-present? raw :exit) (conj out :exit) out)]
                     out)]
    (if (empty? normalized)
      default-collision-phase-mask
      normalized)))

(defn normalize-collision-rule
  "Applies collision-rule defaults and normalization (schema-level only)."
  [rule]
  {:id (get rule :id)
   :slot (or (get rule :slot) default-collision-slot)
   :a-id (get rule :a-id)
   :b-id (get rule :b-id)
   :phase-mask (normalize-collision-phase-mask (get rule :phase-mask))
   :enabled (if (nil? (get rule :enabled)) true (not= (get rule :enabled) false))
   :cooldown-ms (let [v (get rule :cooldown-ms)]
                  (if (nil? v) 0 v))})

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


)TINY_GFX_SCENE"
