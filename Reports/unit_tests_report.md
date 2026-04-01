# Unit-Tests Report

**Stand:** 2026-04-01  
**Quelle:** aktueller Vollsuite-Lauf `./build/unit-tests`

## Übersicht

| Metrik | Wert |
|--------|------|
| **Tests gesamt** | 2,034 |
| **Failures** | 0 |
| **Ignored** | 11 |
| **Exit-Code** | 0 |

## Aktueller Status

- Der komplette Lauf `./build/unit-tests` ist auf dem aktuellen Tree gruen.
- Die frueher separat auffaelligen Breakout-Startup-Tests laufen ebenfalls wieder gruen, sowohl einzeln als auch als ganze Gruppe.
- Das bisherige Parse-/OOM-Narrativ aus aelteren Logs ist fuer den aktuellen Stand ueberholt.

## Auffaellige, aber erwartete Ausgabe im Volllauf

- `ParseError: Unclosed list ...` stammt aus Parser-Negativtests und ist derzeit nur `stderr`-Rauschen, kein Failure.
- `DivisionByZeroError` im Zusammenhang mit `run-next-task` stammt aus Go-Block-/Deferred-Task-Negativtests und ist ebenfalls erwartet.
- `OOM on thread 'unknown'; suppressing exception longjmp.` stammt aus den Hintergrundthread-OOM-Exception-Tests und ist erwartete Diagnoseausgabe.

## Relevante Nachweise

- `./build/unit-tests` -> `2034 Tests, 0 Failures, 11 Ignored`
- `./build/unit-tests --quiet --test 'test_breakout_runtime_startup/*'` -> gruen
- `./build/unit-tests --quiet --test 'test_runtime_stats/*'` -> gruen
- `./build/unit-tests --quiet --test 'test_go_blocks/*'` -> gruen
- `./build/unit-tests --quiet --test 'test_parser/*'` -> gruen
- `./build/unit-tests --quiet --test 'test_exception/*'` -> gruen

## Hinweise

- Fuer leiseres Vollsuite-Output bleibt optional Folgearbeit offen: erwartete Negativtests koennen `stderr` gezielter capturen oder Debugausgaben hinter einen Test-/Verbose-Schalter legen.
- Einzelne Testgruppen weiter wie gewohnt mit `./build/unit-tests --test '<gruppe>/*'` pruefen.
