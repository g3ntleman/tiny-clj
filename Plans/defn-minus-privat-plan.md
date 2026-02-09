# Plan: defn- (private functions) für tiny-clj

## Ziel
- defn- soll wie in Clojure private Funktionen deklarieren.
- Metadaten (wie :doc) bleiben optional.
- Im Debug-Build wird Privatheit enforced, im Release-Build ignoriert.
- Marker: ^:private (bzw. {:private true}) im Metadata-Map der Var.

## Schritte
1. Test für defn- anlegen: Funktion ist im Namespace, aber nicht von außen sichtbar.
2. defn--Makro implementieren: Setzt ^:private und optional {:tiny-clj/private true}.
3. Interpreter/Loader prüft im Debug-Build auf Privatheit und verhindert Zugriff von außen.
4. Release-Build: Privatheit wird ignoriert.
5. Tests für defn- mit und ohne Metadaten.
6. Doku/Plan aktualisieren.

---
Jeder Schritt wird test-first umgesetzt und nach jedem Schritt werden die Unit-Tests ausgeführt.
