---
name: Breakout Collision Migration
overview: Breakout wird schrittweise von handgeschriebenen nativen AABB-Checks auf die vorhandene Collision-Pipeline umgestellt. Der bisherige Durchgang hat den Szenen- und Callback-Vertrag vorbereitet; der aktuelle Folgezug hat die Host-Collision-Bridge auf einen sicheren Zweiphasenpfad umgestellt: Detect-only auf dem UI-/Render-nahen Thread, Raw-Hit-Puffer mit Mutex, Dispatch erst auf dem Scheduler-/Runloop-Thread. Dazu kommen reset-sichere SpatialEvent-/Aabb-Lookups, ein expliziter Interpreter-Thread-Marker neben dem OS-Main-Thread und bessere Test-Isolation im Host-Startup-Pfad. Die native Gameplay-Autoritaet und segmentbasierte Bewegung bleiben Folgeschritte.
status: active
todos:
  - id: scene-prototypes-rules
    content: Breakout-Szene auf entity-map-Root, stabile Ball/Paddle/Brick-Prototypen und deklarative Regeln plus expandierte Policy-Daten umstellen
    status: completed
  - id: deferred-host-dispatch
    content: Host-Collision-Pipeline auf detect-only im UI-Thread, statischen Raw-Hit-Puffer und Dispatch auf dem Scheduler-/Runloop-Thread umstellen
    status: completed
  - id: thread-role-markers
    content: Neben dem OS-Main-Thread auch den Interpreter-/Scheduler-Thread als global testbare Runtime-Rolle markieren
    status: completed
  - id: clojure-policy-expansion
    content: Inkrementelle Policy-Expansion in Clojure mit `CljTransientVector` einfuehren, nur bei Rule-Aenderungen oder neuen Objekten erweitern und Objektentfall lazy behandeln
    status: completed
  - id: callback-dispatch
    content: Breakout-`spatial-callback` in Clojure verdrahten und Watcher fuer Paddle-/Brick-Kollisionen auf Basis von Kollisions-Snapshots implementieren
    status: completed
  - id: runtime-thin-host
    content: Native Breakout-Runtime zu einem duennen Host-Pfad ohne autoritativen Gameplay-State umbauen, expandierte Policies konsumieren und segmentbasierte Ballanimation statt manueller Paddle-/Brick-Kollisionen ausfuehren
    status: completed
  - id: segment-motion
    content: Ballbewegung als von der Grafik-Engine ausgefuehrte Segmente bis zur naechsten Wand modellieren und Replanung nur bei Segmentende oder Kollisions-Event vorsehen
    status: completed
  - id: timeline-end-events
    content: Optionales boolesches End-Event-Feld direkt am Timeline-Feld einfuehren und im Renderer-Snapshot als Metadatum verfuegbar machen
    status: completed
  - id: timeline-end-watchers
    content: Generischen Watch-/Dispatch-Pfad fuer markierte Timeline-Enden einfuehren, damit Segmentenden ohne Breakout-Sondertimer aus dem Engine-Vertrag kommen
    status: completed
  - id: regression-tests
    content: Regressionstests fuer Prototypen, Rule-Expansion, segmentbasierte Ballbewegung, Snapshot-Events und Host-Startpfad ergaenzen
    status: completed
  - id: cleanup
    content: Sourcecode aufraeumen – Debug-Code, temporaere Workarounds, tote Codepfade, ueberfluessige Kommentare und nicht mehr benoetigte Hilfsfunktionen entfernen
    status: pending
  - id: host-bridge-split
    content: Collision-Bridge in kleinere Module fuer Rule-Loading/Event-Encoding versus Detect/Drain aufteilen, damit der alte `viewer_host_spatial.c`-Monolith entfaellt
    status: completed
isProject: false
---

# Breakout auf Collision-Pipeline umstellen

## Stand dieses Durchgangs

- Status: Der Plan ist wieder aktiv. Die Collision-Bridge ist inzwischen weitergezogen als der letzte abgeschlossene Stand; der Host-Monolith ist jetzt in kleinere Runtime- und Rule-/Event-Module aufgeteilt.

- Umgesetzt: `libs/tiny-breakout/scene.clj` liefert jetzt ein entity-map-basiertes `FrameScene.root` mit `'root`-Group, stabilen Ball-/Paddle-/Brick-Prototypen und konkret expandierten `SpatialRule`s fuer `:ball-vs-paddle` sowie jedes aktive `:ball-vs-brick`-Paar.
- Umgesetzt: Tiny-FX trennt fuer Pilot-Szenen jetzt `:root` als Traversal-Einstieg von `:index` als Entity-Map. Breakout und das Game-Demo publizieren diese Form bereits, waehrend der Renderer alte root-map-Szenen vorerst noch lesen kann.
- Umgesetzt: `libs/tiny-clj/deployment.clj` leitet Breakout-`spatial-callback`s jetzt ueber den generischen Dispatcher `tiny-fx.gfx-collision/invoke-collision-callback!`, so dass die bestehende Watcher-Infrastruktur fuer Spatial-Events greift.
- Umgesetzt: `src/game_demo_minifb.c` publiziert native Breakout-Szenen jetzt ebenfalls im neuen `:root`/`:index`-Format; der alte Breakout-Sonderfall zum Beibehalten geladener Rules bei nicht-konformen Roots ist entfallen.
- Umgesetzt: Der Host laedt jetzt alle konkreten Breakout-Policies statt sie bei 16 Eintraegen abzuschneiden, und `SpatialEvent` transportiert zusaetzlich `:self-entity` sowie `:other-entity` aus dem Kollisionsframe. Fuer den Uebergang kann der Host diese Snapshots sowohl aus entity-map-Roots als auch aus dem nativen `Group`-Root aufloesen.
- Umgesetzt: Der native Breakout-Hot-Path aktualisiert Paddle/Ball/HUD/Overlay jetzt in-place auf dem bereits publizierten Szenengraphen; neue Szenen werden nur noch bei echten Layoutwechseln wie Staging-Transition oder Levelwechsel gebaut.
- Umgesetzt: Brick-Records bleiben innerhalb eines Levels stabil und werden bei Treffern nur deaktiviert bzw. unsichtbar gemacht. Dadurch entfaellt das frameweise Neuaufbauen des Szenengraphen bei Brick-Hits.
- Umgesetzt: Regressionstests decken jetzt Root-Shape, Prototyp-/Rule-Vertrag, generischen Spatial-Callback-Dispatch, in-place Scene-Reuse, den `level-clear`-Uebergang zum naechsten Level, konkrete Breakout-Policies im Host-Startup-Pfad und `SpatialEvent`-Entity-Snapshots ab.
- Umgesetzt: Die Host-Collision-Pipeline ist jetzt zweiphasig: `viewer_collision_detect_step()` erkennt nur noch Treffer und puffert rohe Hits, waehrend `viewer_collision_poll_drain()` die `SpatialEvent`-Allokation und den Clojure-Dispatch auf dem Scheduler-/Runloop-Thread uebernimmt.
- Umgesetzt: Roh-Treffer tragen eine Rule-Generation, damit veraltete Hits bei Scene-/Rule-Wechsel nicht mehr gegen eine neue Policy-Generation dispatcht werden.
- Umgesetzt: Host-Tests decken jetzt explizit ab, dass Detect und Dispatch getrennt sind, enge Heap-Grenzen nur noch den Dispatch betreffen und der echte Viewer-Runloop einen global sichtbaren Interpreter-Thread-Marker registriert.
- Umgesetzt: Die Collision-Bridge cached keine `SpatialEvent`-/`Aabb`-Descriptoren oder Symbol-Keys mehr ueber Runtime-Resets hinweg; damit entfallen die bisherigen `[nil nil]`-Events und Crashs nach Test-/Runtime-Resets.
- Umgesetzt: Die Collision-Bridge ist jetzt entlang ihrer Verantwortung getrennt: `viewer_collision_scene_bridge.c` haelt Rule-Expansion und `SpatialEvent`-Encoding, `viewer_collision_dispatch.c` Detect-only, Raw-Hit-Queue und Drain/Dispatch.
- Umgesetzt: Breakout pflegt seine konkreten Collision-Policies jetzt inkrementell in Clojure: `tiny-breakout.scene/with-expanded-collision-rules` fuegt neue Brick-Paare append-only per `transient` hinzu, behaelt vorhandene Regeln stabil und behandelt entfernte Bricks lazy ueber fehlende Scene-Entities.
- Umgesetzt: Der Hostpfad ist inzwischen duenn: `tiny-clj.deployment/breakout-host-config` publiziert nur Slots, Scene-Atom und Spatial-Callback, waehrend Spielzustand, Segmentplanung und Input-Reaktion in `tiny-breakout.runtime` bzw. `tiny-breakout.core` leben.
- Umgesetzt: Ballbewegung laeuft bereits segmentbasiert aus Clojure heraus. `tiny-breakout.core` plant Wandsegmente, `tiny-breakout.scene` projiziert sie auf Timeline-Felder, und `tiny-breakout.runtime` replanted bei Segmentende oder Spatial-Event.
- Umgesetzt: `Timeline` traegt jetzt optional `:end-event`, und der Renderer-Snapshot bzw. `tiny-clj.runtime/renderer-timeline-progress` liefern dazu generische Metadaten (`:end-event`, `:at-end`). Breakout markiert damit bereits Ballsegment-Timelines im Szenenvertrag.
- Umgesetzt: `tiny-fx.gfx-timeline/watch` bildet jetzt einen generischen Timeline-End-Watcher auf dem Scheduler-Thread. Breakout nutzt diesen Pfad fuer Ballsegment-Enden; der alte benannte Segment-Timer ist entfernt.
- Offen: Der direkte native Scene-Rebuild-Pfad allokiert weiterhin deutlich mehr als der in-place Hot-Path. Fuer die vollstaendige Migration ist das tolerierbar, sollte aber nicht als allgemeines Pattern in weitere Hostpfade uebernommen werden.

## Folgearbeit ausserhalb dieses Plans

- `cleanup`: weiterer Abbau alter Kompatibilitaets- und Uebergangslogik

## Zielbild

Breakout nutzt die vorhandene Collision-Pipeline end-to-end, waehrend Clojure die einzige Source of Truth bleibt:

- Die Szene traegt `SpatialRule`-Records in `:collision-rules`.
- Auf dem Host laeuft Breakout kuenftig als expliziter Namespace-Entry innerhalb des vereinheitlichten `tiny-fx`-Produkts beziehungsweise des macOS-Bundles `tiny-fx.app`, nicht mehr als eigenes Runtime-Produkt.
- Ball, Paddle und Bricks bekommen stabile Prototypen; fuer Bricks wird ein gemeinsamer Brick-Prototyp verwendet.
- Clojure haelt zusaetzlich eine expandierte Policy-Menge mit konkreten Entity-Paaren.
- Die Expansion erfolgt inkrementell nur bei Rule-Aenderungen oder wenn neue relevante Objekte hinzukommen.
- Wegfallende Objekte muessen nicht sofort aus der expandierten Menge entfernt werden; fehlende Entities werden im Viewer zur Laufzeit inert, und bei Bedarf kann die konkrete expandierte Rule zusaetzlich direkt deaktiviert werden.
- Wenn der Viewer viele tote oder deaktivierte konkrete Policies sieht, kann C zusaetzlich ein diskretes Maintenance-Event an Clojure werfen, damit die expandierte Policy-Menge gezielt gefiltert oder neu kompiliert wird.
- Die Grafik-Engine animiert den Ball jeweils entlang eines von Clojure geplanten Bewegungssegments bis spaetestens zur naechsten Wand.
- Animationen koennen optional direkt am Timeline-Feld ein boolesches End-Event-Flag tragen; nur diese markierten Felder werden bis zum Ende beobachtet.
- `viewer_collision_detect_step()` arbeitet nur noch auf expandierten Policies und erzeugt daraus `SpatialEvent`s mit den beteiligten Objekten im Kollisionszustand.
- Der Clojure-`spatial-callback` reagiert auf `:ball-vs-paddle` und `:ball-vs-brick`, kann Objekte auf den Kollisions-Snapshot zuruecksetzen und plant von dort aus das naechste Segment.
- `game_state_atom` und `game_scene_atom` bleiben autoritativ und werden nicht in einen langlebigen nativen Gameplay-State ueberfuehrt.
- Die native Seite uebernimmt nur Tick, Snapshot/Render, segmentbasierte Ballanimation, Collision-Detection auf konkreten Policies und die Zustellung diskreter Events an Clojure; End-Events werden nur fuer markierte Timeline-Felder erzeugt.

## Relevante Stellen

- [libs/tiny-breakout/scene.clj](libs/tiny-breakout/scene.clj): baut aktuell `->Group`-Root und setzt `:collision-rules nil`; das muss auf collision-faehige Entities mit Prototypen umgestellt werden.
- [libs/tiny-clj/deployment.clj](libs/tiny-clj/deployment.clj): `breakout-host-spatial-callback!` ist aktuell ein No-op; hier muss die echte Breakout-Collision-Dispatch-Schicht entstehen.
- [src/game_demo_minifb.c](src/game_demo_minifb.c): ist aktuell weiterhin der native Hostpfad hinter `tiny-fx.app`, nutzt aber noch direkte `viewer_breakout_rect_overlap(...)`-Checks, haelt eine eigene Breakout-Runtime als Gameplay-Quelle, expandiert Rules selbst und stept den Ball frameweise; das passt nicht zum neuen Modell.
- [src/viewer_collision.c](src/viewer_collision.c): bestaetigt, dass Prototyp-Selektoren per struktureller Gleichheit funktionieren; darauf kann der Brick-Prototyp aufbauen.
- [src/tiny_fx_gfx.c](src/tiny_fx_gfx.c): registriert `SpatialEvent`; hier muss das Event-Schema fuer echte Kollisions-Snapshots der beteiligten Objekte erweitert werden.
- [src/rendered_state_snapshot.c](src/rendered_state_snapshot.c) und [src/builtins_tiny_fx_gfx.c](src/builtins_tiny_fx_gfx.c): enthalten bereits Timeline-Zustand und Runtime-Abfragen; hier bietet sich ein leichter Opt-in-Pfad fuer Timeline-End-Erkennung an.

## Kritische Architekturpunkte

1. Breakout muss dieselbe Root-Form benutzen, die der Collision-Loader erwartet.
  Heute ist die Breakout-Szene ein `Group`-Baum, der Collision-Loader expandiert Regeln aber ueber eine Entity-Map in `FrameScene.root`. Deshalb muss die Breakout-Szene auf ein entity-map-basiertes Root umgestellt werden, sowohl im Clojure-Build als auch im nativen Breakout-Szenebuilder.
2. Die Policy-Expansion soll in Clojure stattfinden, nicht im Viewer.
  Wegen Ownership, Memory-Macros und globalem Autorelease-Pool ist es einfacher, deklarative Regeln in Clojure mit `CljTransientVector` in konkrete Policies zu expandieren und dem Viewer direkt die expandierte Menge zu geben.
3. Die Expansion soll inkrementell und bewusst simpel bleiben.
  Es sind weniger als 20 deklarative Regeln zu erwarten. Deshalb ist im ersten Wurf kein zusaetzlicher Index noetig; bei neuen Objekten reicht ein linearer Scan ueber alle Regeln und das Anhängen der betroffenen konkreten Policies.
4. Die Ballbewegung soll segmentbasiert statt frameweise modelliert werden.
  Clojure plant jeweils das naechste Bewegungssegment des Balls bis zur naechsten Wand. Die Grafik-Engine animiert dieses Segment autonom, bis es endet oder durch einen Paddle-/Brick-Event unterbrochen wird. So bleibt Clojure diskret, waehrend die Bewegung flach und performant bleibt.
5. Animation-Enden sollen ein expliziter Opt-in-Pfad bleiben.
  Nicht jede Timeline darf automatisch End-Events allozieren oder in den Runloop schieben. Stattdessen traegt nur ein Timeline-Feld bei Bedarf ein boolesches End-Event-Flag; nur dafuer registriert der Host einen Watch und emittiert genau einen End-Event beim Finish-Edge.
6. Heap-Groesse und persistente Datenstrukturen muessen bewusst begrenzt werden.
  Persistente Datenstrukturen sollen nur dann global gehalten werden, wenn sie wirklich wiederverwendet werden. Dauerhafte globale `def`-Werte fuer grosse oder abgeleitete Runtime-Daten sind zu vermeiden; solche Daten sollen nur bei echtem Bedarf gehalten und sonst lokal oder kurzlebig aufgebaut werden.
7. Der native Breakout-Sonderpfad muss seine Gameplay-Autoritaet verlieren.
  Der aktuelle Pfad kopiert den State einmal aus `game_state_atom`, leert das Atom danach und fuehrt Ball/Paddle/Brick-Logik in C weiter. Fuer Clojure als Source of Truth darf dieser Besitzwechsel nicht mehr stattfinden; der Host darf nur noch diskrete Inputs, Segmentende und Spatial-Events an Clojure liefern und die daraus publizierte Szene anzeigen.

```mermaid
flowchart LR
  hostTick[HostTickAndInput] --> cljStep[BreakoutStepInClojure]
  cljStep --> planSegment[PlanNextBallSegment]
  planSegment --> expandPolicies[ExpandPoliciesInClojure]
  expandPolicies --> publishAtoms[Update game_state_atom and game_scene_atom]
  publishAtoms --> viewerRender[ViewerRendersScene]
  publishAtoms --> publishPolicies[Update expandedPolicyAtom]
  publishAtoms --> runSegment[GraphicsEngineAnimatesSegment]
  publishAtoms --> timelineWatch[RegisterEndWatchWhenFlagged]
  publishPolicies --> collisionStep[viewer_collision_detect_step]
  viewerRender --> collisionStep
  timelineWatch --> runSegment
  runSegment --> segmentEnd[SegmentEndEvent]
  segmentEnd --> cljStep
  collisionStep --> spatialEvent[SpatialEventWithSnapshots]
  spatialEvent --> cljCallback[BreakoutSpatialCallback]
  cljCallback --> publishAtoms
  collisionStep --> maintenanceEvent[CleanupEventFromC]
  maintenanceEvent --> expandPolicies
```

## Umsetzungsschritte

1. Collision-faehige Breakout-Szene definieren.
  In [libs/tiny-breakout/scene.clj](libs/tiny-breakout/scene.clj) stabile Prototypen fuer `ball`, `paddle` und `brick` einfuehren, das Root auf eine Entity-Map umstellen und deklarative `SpatialRule`-Records fuer `:ball-vs-paddle` und `:ball-vs-brick` definieren.
2. Inkrementelle Policy-Expansion in Clojure einfuehren.
  In den betroffenen Breakout-/tiny-fx-Namespaces eine expandierte Runtime-Form aufbauen, die deklarative Regeln mit `CljTransientVector` in konkrete Policies ueberfuehrt. Dabei nur bei Rule-Aenderungen oder neuen Objekten anhaengen; bei Objektentfall bleiben alte Policies vorerst bestehen, werden entweder inert oder bei Bedarf per Record-Mutation direkt deaktiviert und erst spaeter optional kompaktierte.
  Dabei Heap-Verbrauch mitdenken: persistente abgeleitete Daten nur dann ueber `def` oder aehnliche globale Bindungen halten, wenn sie wirklich wiederverwendet werden; sonst lokale oder kurzlebige Strukturen bevorzugen.
3. Kollisions-Events zu Snapshot-Events ausbauen.
  In [src/tiny_fx_gfx.c](src/tiny_fx_gfx.c), [src/game_demo_minifb.c](src/game_demo_minifb.c) und den zugehoerigen Tests `SpatialEvent` so erweitern, dass neben IDs und AABBs auch `:self-entity` und `:other-entity` im Zustand des Kollisionsframes transportiert werden.
4. Segmentbasierte Ballbewegung definieren.
  In den Breakout-Namespaces ein Modell einfuehren, bei dem Clojure jeweils das naechste Ballsegment bis zur naechsten Wand plant. Die Grafik-Engine fuehrt dieses Segment aus; bei Segmentende oder Kollisions-Event wird in Clojure neu geplant.
5. Boolesches End-Event-Feld am Timeline-Feld einfuehren.
  In den Timeline-/Runtime-Pfaden einen Opt-in-Mechanismus vorsehen, bei dem ein optionales boolesches Feld direkt am Timeline-Feld einen End-Event-Watch aktiviert. Nur markierte Animationen duerfen beim Finish-Edge genau einen diskreten End-Event an Clojure ausloesen.
6. Breakout-Hostpfad auf atom-getriebenen Betrieb umbauen.
  In [src/game_demo_minifb.c](src/game_demo_minifb.c) den nativen Breakout-Sonderpfad so reduzieren, dass er nicht mehr selbst Ball/Paddle/Brick-State besitzt, Rules expandiert oder den Ball frameweise stept. Stattdessen soll der Host diskrete Button-Ereignisse, Segmentenden und Spatial-Events an Clojure zustellen, die aktuelle Szene aus `game_scene_atom` rendern und konkrete Policies aus einer expandierten Datenstruktur konsumieren.
7. Breakout-Collision-Callback in Clojure verdrahten.
  In [libs/tiny-clj/deployment.clj](libs/tiny-clj/deployment.clj) den Breakout-`spatial-callback` von No-op auf den generischen Dispatcher umstellen und Breakout-spezifische Watcher/Handler registrieren, die aus Snapshot-`SpatialEvent`s deterministisch `ball bounce`, `brick removal`, `score`, `phase` und ggf. `level clear` ableiten.
8. Maintenance-Event fuer Policy-Cleanup vorsehen.
  In [src/game_demo_minifb.c](src/game_demo_minifb.c) und [libs/tiny-clj/deployment.clj](libs/tiny-clj/deployment.clj) einen schmalen Pfad vorsehen, ueber den C bei Bedarf ein diskretes Event an Clojure wirft, wenn expandierte Policies gefiltert, deaktivierte Eintraege bereinigt oder konkret neu aufgebaut werden sollen.
9. Clojure-Step fuer Host-Ticks sauber definieren.
  In [libs/tiny-clj/deployment.clj](libs/tiny-clj/deployment.clj) und den betroffenen Breakout-Namespaces einen klaren Input-/Collision-/Segmentende-/Maintenance-Updatepfad bauen, so dass `step-state`, Policy-Pflege, Segmentplanung und Szenenaufbau in Clojure bleiben und der Host nur noch diskrete Ereignisse liefert. Dabei muessen Paddle-/Brick-Kollisionen und frameweises Ball-Stepping aus dem nativen Sonderpfad entfernt werden.
10. Regressionstests vor/nach der Umstellung absichern.
  Tests fuer Root-Shape, Prototypen, deklarative Rules, inkrementelle Expansion bei neuen Objekten, lazy Objektentfall bzw. Rule-Deaktivierung per Record-Mutation, Snapshot-`SpatialEvent`s mit Objekten, segmentbasierte Ballbewegung bis zur naechsten Wand, boolesche Timeline-End-Events nur fuer markierte Felder, Cleanup per Event aus C, callback-getriebene Brick-Entfernung, Clojure als autoritative State-Quelle und das Fehlen der alten manuellen Kollision in [src/tests/test_breakout_contract.c](src/tests/test_breakout_contract.c), [src/tests/test_breakout_runtime_startup.c](src/tests/test_breakout_runtime_startup.c) und ggf. [src/tests/test_gfx_collision_contract.c](src/tests/test_gfx_collision_contract.c) ergaenzen.
11. Verifikation.
  Relevante Unit-Tests plus Breakout-Start im vereinheitlichten Hostprodukt `tiny-fx` beziehungsweise `tiny-fx.app` ausfuehren und gezielt pruefen, dass Ball/Paddle/Brick jetzt ueber Snapshot-`SpatialEvent`s und Clojure-State reagieren, der Ball zwischen diskreten Replanungen von der Grafik-Engine bis zur naechsten Wand animiert wird, nur markierte Timeline-Felder End-Events erzeugen, neue Bricks nur inkrementell Policies anhaengen, entfernte Bricks keine Kollisionen mehr feuern und keine native Zweitquelle mehr vorhanden ist.

## Besondere Risiken

- Snapshot-basierte Collision-Events koennen ein Frame spaeter kommen als die bisherige direkte Physics-Pruefung; das muss in den Bounce-Regeln und Tests explizit abgesichert werden.
- Wenn Snapshot-Events nicht die beteiligten Objekte selbst enthalten, kann Clojure nur gegen einen spaeteren Live-State reagieren; deshalb muessen echte Objekt-Snapshots Teil des Events sein.
- Wenn der Host noch irgendwo einen eigenen Brick-/Ball-Cache behaelt, drohen Ghost-Collisions oder doppelte Hits; die Umstellung muss solche nativen Zweitquellen vollstaendig entfernen oder strikt auf Render-Caches begrenzen.
- Die append-only Expansion braucht eine Duplikat-Sicherung, damit neu erscheinende Objekte konkrete Policies nicht mehrfach anhaengen.
- Lazy Removal darf nicht zu dauerhaft wachsenden Policy-Mengen fuehren; falls konkrete Policies bei Objektentfall per Record-Mutation deaktiviert werden, muss diese Mutation eindeutig und testbar sein. Eine spaetere opportunistische Kompaktierung sollte als Folgeschritt moeglich bleiben, aber nicht Teil des ersten Wurfs sein.
- Ein Maintenance-Event aus C darf nicht im Hot-Path spammen; es braucht eine klare Trigger-Bedingung oder Entprellung, damit Cleanup/Filtern nur bei echtem Bedarf angestossen wird.
- Die Umstellung auf entity-map-Root darf Render-Reihenfolge und Sichtbarkeit der bestehenden Breakout-Szene nicht veraendern.
- Segmentende, Wandkontakt und Paddle-/Brick-Kollision duerfen sich nicht widersprechen; die Prioritaet zwischen Segment-Ende und vorzeitigem Collision-Event muss klar definiert und getestet werden.
- Boolesche Timeline-End-Events duerfen keinen allgemeinen Render-Overhead erzeugen; unmarkierte Animationen muessen komplett ohne Watch-Registrierung und Event-Allokation bleiben.
- Grosse persistente oder global gehaltene Hilfsstrukturen koennen die Heap-Grenze unnoetig belasten; globale `def`-Caches sind deshalb nur zulaessig, wenn ihr Wiederverwendungswert den Speicherverbrauch rechtfertigt.
