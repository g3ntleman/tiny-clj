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

## Praktische Tipps

- **Relevante Signale:** Exception bei Double-Free, Pool-Assert, Crashes in `release` / `DEALLOC` / `autorelease_pool_drain`
- **Zombie-Mode abschalten:** CMake `-DTINYCLJ_ZOMBIE_ENABLED=OFF` bzw. subjective-c ohne `ZOMBIE_ENABLED` – dann wird wirklich `free` aufgerufen, weniger RAM, aber Crashes bei Fehlern.
