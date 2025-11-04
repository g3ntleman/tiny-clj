# Unterschiede zwischen tiny-clj go-Blöcken und Clojure core.async go-Blöcken

## Übersicht

Die tiny-clj Implementierung von go-Blöcken ist eine **vereinfachte, minimal-kompatible Version** im Vergleich zu Clojures core.async. Die Hauptunterschiede sind:

## 1. Makro-Expansion vs. Laufzeit-Wrapping

### Clojure (core.async)
- **Compile-Zeit**: Der `go`-Makro expandiert den Code zur Compile-Zeit in eine Zustandsmaschine
- **Zustandsmaschine**: Der Code wird in eine State Machine umgewandelt, die bei `<`-Operationen parken kann
- **Makro-Übersetzung**: Komplexe Makro-Logik übersetzt den Code vor der Ausführung

### tiny-clj
- **Laufzeit-Wrapping**: Der Body wird zur Laufzeit in eine nullstellige Funktion `(fn [] body)` gewrappt
- **Keine Zustandsmaschine**: Keine Compile-Zeit-Übersetzung
- **Einfache Funktions-Wraps**: Direkte Funktionserstellung zur Laufzeit

```clojure
;; Clojure: Makro-Expansion zur Compile-Zeit
(go (do 1 2 3))
;; → Wird zu einer Zustandsmaschine expandiert

;; tiny-clj: Laufzeit-Wrapping
(go (do 1 2 3))
;; → Wird zu (fn [] (do 1 2 3)) gewrappt und in Queue eingereiht
```

## 2. Thread-Pool vs. Manuelle Ausführung

### Clojure (core.async)
- **Thread-Pool**: Verwendet einen internen Thread-Pool (standardmäßig 8 Threads)
- **Automatische Ausführung**: Go-Blöcke werden automatisch vom Thread-Pool ausgeführt
- **Park-Mechanismus**: Threads werden "geparkt" statt blockiert bei `<`-Operationen
- **Konfigurierbar**: Pool-Größe über `clojure.core.async.pool-size` System-Property

### tiny-clj
- **Keine Thread-Pool**: Keine automatische Thread-Verwaltung
- **Manuelle Ausführung**: Go-Blöcke müssen manuell über `(run-next-task)` ausgeführt werden
- **FIFO-Queue**: Einfache FIFO-Queue für Task-Verwaltung
- **Synchron**: Ausführung erfolgt synchron im aktuellen Thread

```clojure
;; Clojure: Automatische Ausführung im Thread-Pool
(go (println "Hello"))  ;; Wird automatisch ausgeführt

;; tiny-clj: Manuelle Ausführung erforderlich
(go (println "Hello"))  ;; Wird nur ausgeführt, wenn (run-next-task) aufgerufen wird
```

## 3. Park-Mechanismus vs. Blocking

### Clojure (core.async)
- **Parking**: Operationen wie `<!` (take) und `>!` (put) parken den Thread
- **Nicht-blockierend**: Der Thread bleibt für andere Tasks verfügbar
- **Zustandsmaschine**: Der Code wird bei Park-Operationen in einen neuen State überführt
- **Kanäle**: Echte bidirektionale Kanäle mit Take/Put-Operationen

### tiny-clj
- **Kein Parking**: Keine Park-Mechanismen
- **Blocking**: Langlaufende Operationen blockieren den aktuellen Thread
- **Result-Channel**: Einfache Result-Channels `{:value result :closed true}`
- **Keine Take/Put**: Keine `<`- oder `>`-Operationen innerhalb von go-Blöcken

```clojure
;; Clojure: Park-Mechanismus
(go 
  (let [val (<! ch)]  ;; Thread wird geparkt, wartet auf Wert
    (println val)))

;; tiny-clj: Keine Park-Mechanismen
(go 
  (println "Hello"))  ;; Muss vollständig ausgeführt werden, keine Park-Optionen
```

## 4. Channel-Operationen

### Clojure (core.async)
- **Bidirektionale Kanäle**: Echte Channel-Objekte mit Take/Put-Operationen
- **`<!` (take)**: Nimmt einen Wert aus einem Channel (parkt Thread)
- **`>!` (put)**: Schreibt einen Wert in einen Channel (parkt Thread)
- **`<!` außerhalb go**: `<!` kann nur innerhalb von go-Blöcken verwendet werden
- **`<!!` (blocking take)**: Blockierender Take außerhalb von go-Blöcken

### tiny-clj
- **Result-Channels**: Einfache Map-basierte Result-Channels
- **Keine Take/Put**: Keine `<`- oder `>`-Operationen
- **Rückgabewert**: Go-Block gibt einen Channel zurück: `{:value result :closed true}`
- **Manuelles Lesen**: Werte müssen manuell aus dem Channel gelesen werden

```clojure
;; Clojure: Channel-Operationen
(go
  (let [ch (chan)]
    (>! ch 42)  ;; Put in Channel
    (println (<! ch))))  ;; Take from Channel

;; tiny-clj: Result-Channel
(go 42)
;; → Gibt Channel zurück: {:value 42 :closed true}
;; → Wert muss manuell aus Channel gelesen werden
```

## 5. Exception Handling

### Clojure (core.async)
- **Exception im go-Block**: Wird zum Channel als Exception-Wert gepusht
- **Exception Handling**: Kann über Channel-Werte behandelt werden

### tiny-clj
- **Exception Handling**: Exceptions werden abgefangen
- **Channel wird geschlossen**: Bei Exception wird Channel geschlossen ohne Wert
- **Kein Exception-Wert**: Exception wird nicht als Wert im Channel gespeichert

## 6. Kompatibilität und Einschränkungen

### Clojure (core.async)
- **Makro-Limitierungen**: Bestimmte Konstrukte (z.B. `map`, `for`) funktionieren nicht innerhalb von go-Blöcken, da sie Closures erstellen
- **Funktionsgrenzen**: Makro-Übersetzung stoppt an Funktionsgrenzen

### tiny-clj
- **Einfacherer Ansatz**: Keine Makro-Expansion, daher weniger Einschränkungen
- **Direkter Code**: Body wird direkt als Funktion ausgeführt
- **Einschränkung**: Keine Channel-Operationen innerhalb von go-Blöcken

## Zusammenfassung

| Feature | Clojure core.async | tiny-clj |
|---------|-------------------|----------|
| Makro-Expansion | ✅ Zustandsmaschine zur Compile-Zeit | ❌ Laufzeit-Wrapping |
| Thread-Pool | ✅ Automatisch (8 Threads) | ❌ Manuelle Ausführung |
| Park-Mechanismus | ✅ `<!` und `>!` parken Threads | ❌ Kein Parking |
| Channel-Operationen | ✅ Bidirektionale Kanäle | ❌ Nur Result-Channels |
| Take/Put | ✅ `<`- und `>`-Operationen | ❌ Keine Take/Put |
| Automatische Ausführung | ✅ Thread-Pool | ❌ Manuell via `run-next-task` |
| Exception als Wert | ✅ Ja | ❌ Nein, Channel wird geschlossen |

## Fazit

Die tiny-clj Implementierung ist eine **minimal-kompatible Version** für einfache asynchrone Aufgaben. Sie eignet sich für:
- Einfache asynchrone Berechnungen
- Task-Queue-Management
- Grundlegende Go-Block-Semantik

Sie **eignet sich nicht** für:
- Komplexe asynchrone Workflows mit Channel-Kommunikation
- Thread-Pool-basierte Parallelisierung
- Park-Mechanismen für blockierende Operationen

Die Implementierung ist bewusst vereinfacht, um die Komplexität gering zu halten und für Embedded-Systeme geeignet zu sein.

