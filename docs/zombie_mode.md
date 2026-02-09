# Zombie-Mode

## Was bedeutet Zombie-Mode?

Wenn `ZOMBIE_ENABLED` gesetzt ist (z.B. in Debug-Builds von subjective-c), werden Objekte bei `rc=0` **nicht freigegeben**, sondern als „Zombie“ markiert. Zombies sind normal im Zombie-Mode und werden nicht gemeldet. Der Modus dient nur dazu, Crashes beim Debuggen zu vermeiden – auf Kosten von RAM.

---

## Double-Free (rc=0 bei RELEASE)

Wenn `RELEASE` auf ein Objekt mit `rc=0` aufgerufen wird (Doppelfreigabe), wird eine Exception ausgelöst (`UseAfterFreeError`). Typische Ursachen:

- `RELEASE` auf etwas, das nur aus dem Pool/Calleepfad kommt
- `AUTORELEASE` an der falschen Stelle (z.B. Aufrufer und Callee)
- `ASSIGN`/manuelles Release, das denselben Besitz zweimal abgibt

---

## Pool-Assert

Assert `"Object v still in autorelease pool; will double-release"`: Das Objekt wird released, obwohl es noch im Autorelease-Pool steht → würde beim Drain ein zweites Mal released. Meist: `RELEASE` auf Pool-Objekt oder falsches `AUTORELEASE`.

---

## Warum trotz ZOMBIE_ENABLED Crashes/Aborts auftreten können

Bei ZOMBIE_ENABLED wird **kein** Speicher freigegeben (weder Zombie noch Kinder – alles bleibt allokiert). Trotzdem können Aborts/Exceptions entstehen:

1. **zombie_description / clj_to_string auf Zombie (rc=0)**  
   Im Pfad von `clj_to_string(v)` werden oft **RETAIN** oder **AUTORELEASE** auf Objekte angewendet (z. B. beim Aufbau von Teilergebnissen). Hat `v` oder ein Kind bereits rc=0, löst **RETAIN** dort gezielt **ZombieAccessException** aus („rc must be > 0“). Man bekommt also eine zweite Exception/ einen Abort *während* man die erste Meldung (Double-Free o. ä.) formatiert. Der Speicher ist nicht kaputt – die *Refcount-Prüfungen* im Code werfen.

2. **autorelease während Drain**  
   Wird im Drain-Pfad (z. B. indirekt in `release_object_deep` oder in Destruktoren) erneut `autorelease` aufgerufen, löst die Prüfung „autorelease during drain“ Assert/Abort aus.

**Praktisch:** Damit beim Melden eines Zombie-Problems nicht sofort ein zweiter Fehler (RETAIN auf rc=0) ausgelöst wird, ruft `zombie_description` bei rc≤0 kein `clj_to_string(v)` mehr auf, sondern nur noch „(zombie %p)“. RCHIST und Backtrace reichen zur Fehlersuche.

## Praktische Tipps

- **Relevante Signale:** Exception bei Double-Free, Pool-Assert, Crashes in `release` / `DEALLOC` / `autorelease_pool_drain` oder **in `zombie_description`/`clj_to_string`** (sekundär beim Formatieren des Zombie).
- **Zombie-Mode abschalten:** CMake `-DZOMBIE_ENABLED=OFF` bzw. subjective-c ohne `ZOMBIE_ENABLED` – dann wird wirklich `free` aufgerufen, weniger RAM, aber Crashes bei Fehlern.
