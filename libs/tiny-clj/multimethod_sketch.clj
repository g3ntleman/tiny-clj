;; ============================================================================
;; Multi-Method Implementation Sketch (Pure Clojure)
;; ============================================================================
;; 
;; Diese Datei zeigt, wie Multi-Methods in pure Clojure implementiert
;; werden könnten, wenn man Maps, Vektoren und Funktionen hat.
;;
;; Multi-Methods benötigen:
;; 1. Method-Registry (Map von Dispatch-Wert -> Methode)
;; 2. Hierarchie-System (derive/isa?)
;; 3. Dispatch-Funktion
;; 4. Method-Auswahl zur Laufzeit
;; ============================================================================

;; ----------------------------------------------------------------------------
;; Globale Registry für Multi-Methods
;; ----------------------------------------------------------------------------
;; Struktur: {multimethod-name {:dispatch-fn fn, :methods {dispatch-value -> method-fn}}}
;; In einer echten Implementierung würde dies in einem Atom oder Namespace gespeichert

(def multi-method-registry (atom {}))

;; ----------------------------------------------------------------------------
;; Hierarchie-System (derive/isa?)
;; ----------------------------------------------------------------------------
;; Struktur: {child -> #{parent1 parent2 ...}}
;; In einer echten Implementierung würde dies in einem Atom gespeichert

(def hierarchy-registry (atom {}))

^#^{:doc "Fügt eine Hierarchie-Beziehung hinzu: tag ist ein Kind von parent."}
(defn derive [tag parent]
  (swap! hierarchy-registry 
         (fn [h]
           (update h tag (fn [parents]
                          (if parents
                            (conj parents parent)
                            #{parent}))))))

^#^{:doc "Gibt die direkten Eltern (Set) eines Tags zurück."}
(defn parents [tag]
  (get @hierarchy-registry tag))

^#^{:doc "Gibt alle Vorfahren eines Tags zurück (transitive closure)."}
(defn ancestors [tag]
  (let [direct-parents (parents tag)]
    (if (empty? direct-parents)
      #{}
      (loop [result direct-parents
             to-process direct-parents]
        (if (empty? to-process)
          result
          (let [current (first to-process)
                current-parents (parents current)
                new-parents (clojure.set/difference current-parents result)]
            (recur (clojure.set/union result new-parents)
                   (concat (rest to-process) new-parents))))))))

^#^{:doc "Prüft, ob child ein Kind von parent ist (inkl. transitiver Beziehungen)."}
(defn isa? [child parent]
  (or (= child parent)
      (contains? (ancestors child) parent)))

;; ----------------------------------------------------------------------------
;; defmulti - Definiert eine Multi-Method
;; ----------------------------------------------------------------------------
;; Syntax: (defmulti name dispatch-fn)
;; 
;; In einer echten Implementierung würde dies:
;; 1. Eine Var im Namespace erstellen
;; 2. Die Dispatch-Funktion speichern
;; 3. Eine leere Method-Registry initialisieren

^#^{:doc "Erstellt eine neue Multi-Method und registriert sie in multi-method-registry."}
(defn defmulti-impl [name dispatch-fn]
  (swap! multi-method-registry 
         (fn [registry]
           (assoc registry name {:dispatch-fn dispatch-fn
                                :methods {}
                                :prefer-table {}})))
  ;; In echter Implementierung: Var im Namespace erstellen
  ;; (def name (fn [& args] (multi-method-dispatch name args)))
  name)

;; Beispiel:
;; (defmulti-impl :area (fn [shape] (type shape)))
;; (defmulti-impl :draw (fn [shape] [(type shape) (:color shape)]))

;; ----------------------------------------------------------------------------
;; defmethod - Fügt eine Methode zu einer Multi-Method hinzu
;; ----------------------------------------------------------------------------
;; Syntax: (defmethod multi-name dispatch-value [params] body)

^#^{:doc "Fügt eine Methode (method-fn) für dispatch-value zu einer Multi-Method hinzu."}
(defn defmethod-impl [multi-name dispatch-value method-fn]
  (swap! multi-method-registry
         (fn [registry]
           (let [mm (get registry multi-name)]
             (if mm
               (assoc registry multi-name
                      (update mm :methods assoc dispatch-value method-fn))
               (throw (Exception. (str "Multi-method " multi-name " not found")))))))
  multi-name)

;; ----------------------------------------------------------------------------
;; Method-Auswahl zur Laufzeit
;; ----------------------------------------------------------------------------

^#^{:doc "Findet die passende Methode für dispatch-value aus methods (exakt oder via Hierarchie)."}
(defn find-matching-method [methods dispatch-value]
  ;; 1. Exakte Übereinstimmung
  (if-let [method (get methods dispatch-value)]
    method
    ;; 2. Hierarchie-basierte Suche
    (let [ancestors-set (ancestors dispatch-value)]
      (loop [candidates (filter (fn [[dv _]] 
                                  (isa? dispatch-value dv)) 
                                methods)]
        (cond
          (empty? candidates) nil
          (= (count candidates) 1) (second (first candidates))
          :else
          ;; Mehrere Kandidaten: nimm den spezifischsten (kleinste Ancestor-Menge)
          (let [sorted (sort-by (fn [[dv _]]
                                  (count (ancestors dv)))
                                candidates)]
            (second (first sorted))))))))

^#^{:doc "Dispatcht eine Multi-Method anhand ihres :dispatch-fn und wendet die passende Methode auf args an."}
(defn multi-method-dispatch [multi-name args]
  (let [mm (get @multi-method-registry multi-name)]
    (if (not mm)
      (throw (Exception. (str "Multi-method " multi-name " not found"))))
    (let [dispatch-fn (:dispatch-fn mm)
          dispatch-value (apply dispatch-fn args)
          methods (:methods mm)
          method (find-matching-method methods dispatch-value)]
      (if method
        (apply method args)
        (throw (Exception. (str "No method for dispatch value: " dispatch-value)))))))

;; ----------------------------------------------------------------------------
;; Beispiel-Verwendung
;; ----------------------------------------------------------------------------

;; 1. Multi-Method definieren
;; (defmulti-impl :area (fn [shape] (type shape)))

;; 2. Hierarchie definieren
;; (derive :rectangle :shape)
;; (derive :square :rectangle)
;; (derive :circle :shape)

;; 3. Methoden definieren
;; (defmethod-impl :area :rectangle 
;;   (fn [shape] (* (:width shape) (:height shape))))
;; 
;; (defmethod-impl :area :square
;;   (fn [shape] (* (:side shape) (:side shape))))
;; 
;; (defmethod-impl :area :circle
;;   (fn [shape] (* Math/PI (:radius shape) (:radius shape))))

;; 4. Verwendung
;; (multi-method-dispatch :area [{:type :square :side 5}])
;; => 25

;; ----------------------------------------------------------------------------
;; Erweiterungen
;; ----------------------------------------------------------------------------

;; prefer-method: Priorität für Methoden mit gleicher Spezifität
^#^{:doc "Setzt eine Präferenz: dispatch-val-x wird dispatch-val-y vorgezogen (bei gleicher Spezifität)."}
(defn prefer-method-impl [multi-name dispatch-val-x dispatch-val-y]
  (swap! multi-method-registry
         (fn [registry]
           (let [mm (get registry multi-name)]
             (if mm
               (assoc registry multi-name
                      (update mm :prefer-table 
                              (fn [pref]
                                (update pref dispatch-val-x
                                        (fn [preferred]
                                          (if preferred
                                            (conj preferred dispatch-val-y)
                                            #{dispatch-val-y}))))))
               registry)))))

;; remove-method: Entfernt eine Methode
^#^{:doc "Entfernt die Methode für dispatch-value aus einer Multi-Method."}
(defn remove-method-impl [multi-name dispatch-value]
  (swap! multi-method-registry
         (fn [registry]
           (let [mm (get registry multi-name)]
             (if mm
               (assoc registry multi-name
                      (update mm :methods dissoc dispatch-value))
               registry)))))

;; ----------------------------------------------------------------------------
;; Integration mit defrecord (wenn verfügbar)
;; ----------------------------------------------------------------------------
;; 
;; Wenn defrecord verfügbar ist, könnte man Multi-Methods so verwenden:
;;
;; (defrecord Rectangle [width height])
;; (defrecord Circle [radius])
;;
;; (defmulti-impl :area (fn [shape] (type shape)))
;; (defmethod-impl :area Rectangle (fn [r] (* (:width r) (:height r))))
;; (defmethod-impl :area Circle (fn [c] (* Math/PI (:radius c) (:radius c))))
;;
;; Oder mit Hierarchie:
;; (derive Rectangle :shape)
;; (derive Circle :shape)
;; (defmethod-impl :area :shape (fn [s] 0)) ; Default-Methode

;; ----------------------------------------------------------------------------
;; Notizen zur Implementierung in tiny-clj
;; ----------------------------------------------------------------------------
;;
;; Für eine echte Implementierung in tiny-clj müsste man:
;;
;; 1. **Builtins/Special Forms**:
;;    - defmulti als Special Form oder Builtin
;;    - defmethod als Special Form oder Builtin
;;    - derive als Builtin
;;    - isa? als Builtin
;;
;; 2. **Datenstrukturen**:
;;    - Registry in einem Atom oder Namespace-Var speichern
;;    - Hierarchie als Map von Tag -> Set von Parents
;;
;; 3. **Dispatch-Mechanismus**:
;;    - In eval_function_call integrieren
;;    - Wenn eine Funktion aufgerufen wird, prüfen ob es eine Multi-Method ist
;;    - Dispatch-Wert berechnen
;;    - Passende Methode finden und aufrufen
;;
;; 4. **Performance**:
;;    - Method-Lookup könnte gecacht werden
;;    - Hierarchie-Berechnung könnte memoized werden
;;    - Dispatch-Funktion sollte schnell sein (im hot path)
;;
;; 5. **Memory**:
;;    - Registry sollte effizient gespeichert werden
;;    - Hierarchie-Sets sollten nicht dupliziert werden
;;    - Method-Funktionen sollten referenziert, nicht kopiert werden

