# REPL-Funktionen: Implementierungsstatus und Anforderungen

## Übersicht

Die REPL-Helper-Funktionen in `clojure.repl` haben unterschiedliche Implementierungsgrade. Einige sind vollständig implementiert, andere sind vereinfacht und benötigen erweiterte Features.

## Vollständig implementierte Funktionen

### `doc`
- **Status**: ✅ Vollständig implementiert
- **Funktionalität**: Zeigt Metadaten (Name, Parameterlisten, Dokumentation) einer Funktion/Variable
- **Abhängigkeiten**: Metadaten-Support (bereits vorhanden)

### `source`
- **Status**: ✅ Vollständig implementiert
- **Funktionalität**: Zeigt den Quellcode einer Funktion
- **Implementierung**: Native Implementierung ohne Abhängigkeiten

### `dir`
- **Status**: ✅ Vollständig implementiert
- **Funktionalität**: Listet alle öffentlichen Funktionen in einem Namespace auf
- **Abhängigkeiten**: `find-ns`, `ns-map` (bereits vorhanden)

## Vereinfachte Implementierungen

### `pst` - Print Stack Trace

#### Aktuelle Implementierung
```clojure
(defn pst []
  (println "Stack trace not yet fully implemented"))
```

#### Was `pst` in Clojure/JVM macht
In Clojure/JVM druckt `pst` den Stack-Trace der **zuletzt aufgetretenen Exception**:

```clojure
user=> (/ 1 0)
ArithmeticException Divide by zero  clojure.lang.Numbers.divide (Numbers.java:188)

user=> (pst)
ArithmeticException Divide by zero
        clojure.lang.Numbers.divide (Numbers.java:188)
        clojure.core$divide.invokeStatic (core.clj:1553)
        clojure.core$divide.invoke (core.clj:1523)
        user$eval1.invokeStatic (form-init123456.clj:1)
        user$eval1.invoke (form-init123456.clj:1)
        clojure.lang.Compiler.eval (Compiler.java:7177)
        ...
```

#### Was für eine vollständige Implementierung benötigt wird

1. **Exception-Stack-Tracking**:
   - Die Runtime muss die **zuletzt aufgetretene Exception** speichern
   - Aktuell werden Exceptions nur während `TRY/CATCH`-Blöcken gehalten
   - Nach dem `CATCH`-Block wird die Exception nicht mehr verfügbar

2. **Clojure-Level Stack-Trace**:
   - Aktuell haben wir nur **C-Level Stack-Traces** (via `backtrace()`)
   - Diese zeigen C-Funktionen wie `eval_list`, `eval_function_call`, etc.
   - Clojure/JVM zeigt **Clojure-Funktionen** wie `clojure.core/divide`, `user$eval1`, etc.

3. **EvalState-Integration**:
   - Die Exception muss mit dem `EvalState` verknüpft werden
   - Der Stack-Trace muss Clojure-Funktionsnamen und Namespaces zeigen
   - Aktuell: `eval_list` → `eval_function_call` → `native_divide`
   - Gewünscht: `user/eval1` → `clojure.core/divide`

4. **Exception-Persistenz**:
   ```c
   // In EvalState oder Runtime:
   CLJException *last_exception;  // Zuletzt aufgetretene Exception
   
   // In TRY/CATCH:
   TRY {
       // ...
   } CATCH(ex) {
       // Speichere Exception für pst
       g_runtime.last_exception = ex;
       // ...
   } END_TRY
   ```

5. **Clojure-Stack-Trace-Generierung**:
   - Mapping von C-Funktionen zu Clojure-Funktionen
   - Verwendung von Metadaten aus Funktionen
   - Anzeige von Namespace-Namen und Funktionsnamen

#### Implementierungsplan für `pst`

```clojure
(defn pst []
  (if-let [ex *e]  ; *e ist die zuletzt aufgetretene Exception
    (do
      (println (str (:type ex) ": " (:message ex)))
      (if-let [st (:stacktrace ex)]
        (println st))
      nil)
    (println "No exception available")))
```

**Benötigte C-Änderungen**:
- `*e` Variable im `EvalState` oder `Runtime`
- Speicherung der letzten Exception nach jedem `CATCH`
- Clojure-Level Stack-Trace-Generierung

---

### `find-doc` - Find Documentation

#### Aktuelle Implementierung
```clojure
(defn find-doc [pattern]
  (println "find-doc: Searching documentation...")
  (println "Note: Full documentation search requires enhanced metadata support"))
```

#### Was `find-doc` in Clojure/JVM macht
Durchsucht **alle geladenen Namespaces** nach Dokumentationsstrings, die das Pattern enthalten:

```clojure
user=> (find-doc "reverse")
-------------------------
clojure.core/reverse
([coll])
  Returns a seq of the items in coll in reverse order. Not lazy.
-------------------------
clojure.string/reverse
([s])
  Returns s with its characters reversed.
-------------------------
```

#### Was für eine vollständige Implementierung benötigt wird

1. **Namespace-Enumeration**:
   - Durchlaufen aller geladenen Namespaces
   - Aktuell: `ns_find()` für einzelne Namespaces
   - Benötigt: Iteration über `g_runtime.ns_registry`

2. **Metadaten-Durchsuchung**:
   - Für jede Variable/Funktion in jedem Namespace:
     - Metadaten abrufen (`meta`)
     - Dokumentationsstring extrahieren (`:doc`)
     - Pattern-Matching durchführen

3. **String-Suchfunktion**:
   - Clojure-Level String-Suche (nicht nur C `strstr`)
   - Case-insensitive Option (wie in Clojure/JVM)
   - Regex-Support (optional, wie in Clojure/JVM)

4. **Formatierte Ausgabe**:
   - Gleiche Formatierung wie `doc` (25 Bindestriche, Name, Parameter, Doc)
   - Gruppierung nach Namespace

#### Implementierungsplan für `find-doc`

```clojure
(defn find-doc [pattern]
  (let [pattern-lower (lower-case pattern)
        matches (atom [])]
    ;; Durchlaufe alle Namespaces
    (doseq [ns-name (all-loaded-namespaces)]
      (let [ns-obj (find-ns ns-name)
            ns-map (if ns-obj (ns-map ns-obj) {})]
        ;; Durchlaufe alle Funktionen im Namespace
        (doseq [[sym-name var] ns-map]
          (if-let [m (meta var)]
            (if-let [doc-str (:doc m)]
              ;; Suche Pattern im Dokumentationsstring
              (if (includes? (lower-case doc-str) pattern-lower)
                (swap! matches conj [ns-name sym-name m doc-str])))))))
    ;; Gib alle Matches aus
    (if (empty? @matches)
      (println "No matching documentation found")
      (doseq [[ns-name sym-name m doc-str] @matches]
        (doc var)))))
```

**Benötigte C-Änderungen**:
- `all-loaded-namespaces` Funktion (iteriert über `ns_registry`)
- `includes?` oder `index-of` für String-Suche
- `lower-case` Funktion (bereits vorhanden in `clojure.string`)
- Atom-Support für `matches` (oder alternativ List-Building)

**Alternative ohne Atoms**:
```clojure
(defn find-doc [pattern]
  (let [pattern-lower (lower-case pattern)
        ;; Sammle Matches in einer Liste
        matches (reduce (fn [acc ns-name]
                          (let [ns-obj (find-ns ns-name)
                                ns-map (if ns-obj (ns-map ns-obj) {})]
                            (reduce (fn [acc2 [sym-name var]]
                                      (if-let [m (meta var)]
                                        (if-let [doc-str (:doc m)]
                                          (if (includes? (lower-case doc-str) pattern-lower)
                                            (cons [ns-name sym-name m doc-str] acc2)
                                            acc2))
                                        acc2))
                                    acc
                                    ns-map)))
                        (list)
                        (all-loaded-namespaces))]
    (if (empty? matches)
      (println "No matching documentation found")
      (doseq [[ns-name sym-name m doc-str] matches]
        (doc var)))))
```

---

## Zusammenfassung: Fehlende Features

### Für `pst`:
1. ✅ Exception-System vorhanden
2. ❌ Exception-Persistenz (letzte Exception speichern)
3. ❌ Clojure-Level Stack-Trace (nur C-Level vorhanden)
4. ❌ `*e` Variable für Zugriff auf letzte Exception

### Für `find-doc`:
1. ✅ Metadaten-Support vorhanden
2. ✅ `meta` Funktion vorhanden
3. ❌ Namespace-Enumeration (`all-loaded-namespaces`)
4. ❌ String-Suchfunktion (`includes?` oder ähnlich)
5. ❌ Case-insensitive String-Vergleich

## Priorisierung

**Niedrige Priorität**:
- Beide Funktionen sind "nice-to-have"
- Aktuelle vereinfachte Implementierungen geben hilfreiche Fehlermeldungen
- Vollständige Implementierung erfordert signifikante Architektur-Änderungen

**Mittlere Priorität** (wenn implementiert):
- `find-doc` ist nützlicher für Entwickler (Dokumentationssuche)
- `pst` ist nützlich für Debugging, aber C-Stack-Traces sind bereits verfügbar

**Hohe Komplexität**:
- Beide Funktionen erfordern neue Runtime-Features
- Clojure-Level Stack-Traces erfordern AST-Tracking während Evaluation
- Namespace-Enumeration erfordert Registry-Iteration


