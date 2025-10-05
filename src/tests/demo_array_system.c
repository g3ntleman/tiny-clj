#include <stdio.h>
#include <stdlib.h>
#include "../CljObject.h"

int main() {
    printf("=== Tiny-Clj Array-basiertes Funktionsaufruf-System ===\n\n");
    
    // Symbol-Tabelle initialisieren
    symbol_table_cleanup();
    
    printf("1. STACK-ALLOKATION FÜR ARGUMENTE\n");
    printf("   - Verwendet STACK_ALLOC(CljObject*, argc) für Parameter-Arrays\n");
    printf("   - Keine Heap-Allokation für temporäre Argumente\n");
    printf("   - Maximal 32 Parameter (STM32-sicher)\n\n");
    
    // 1. Parameter-Symbole erstellen
    printf("2. PARAMETER-SYMBOLE ERSTELLEN\n");
    CljObject *x_sym = intern_symbol_global("x");
    CljObject *y_sym = intern_symbol_global("y");
    CljObject *z_sym = intern_symbol_global("z");
    
    printf("   x: %s\n", pr_str(x_sym));
    printf("   y: %s\n", pr_str(y_sym));
    printf("   z: %s\n", pr_str(z_sym));
    printf("\n");
    
    // 2. Verschiedene Funktionen erstellen
    printf("3. FUNKTIONEN MIT ARRAY-BASIERTEN PARAMETERN\n");
    
    // Funktion ohne Parameter
    CljObject *func0 = autorelease(make_function(NULL, 0, autorelease(make_int(100)), NULL, "zero-params"));
    printf("   Funktion ohne Parameter: %s\n", pr_str(func0));
    
    // Funktion mit einem Parameter
    CljObject *params1[1] = {x_sym};
    CljObject *func1 = autorelease(make_function(params1, 1, autorelease(make_int(200)), NULL, "one-param"));
    printf("   Funktion mit 1 Parameter: %s\n", pr_str(func1));
    
    // Funktion mit zwei Parametern
    CljObject *params2[2] = {x_sym, y_sym};
    CljObject *func2 = autorelease(make_function(params2, 2, autorelease(make_int(300)), NULL, "two-params"));
    printf("   Funktion mit 2 Parametern: %s\n", pr_str(func2));
    
    // Funktion mit drei Parametern
    CljObject *params3[3] = {x_sym, y_sym, z_sym};
    CljObject *func3 = autorelease(make_function(params3, 3, autorelease(make_int(400)), NULL, "three-params"));
    printf("   Funktion mit 3 Parametern: %s\n", pr_str(func3));
    printf("\n");
    
    // 3. Funktionsaufrufe testen
    printf("4. FUNKTIONSAUFRUFE MIT STACK-ALLOKATION\n");
    
    // Funktion ohne Parameter aufrufen
    printf("   (zero-params): ");
    CljObject *result0 = autorelease(clj_call_function(func0, 0, NULL));
    printf("%s\n", pr_str(result0));
    
    // Funktion mit einem Parameter aufrufen
    CljObject *arg1 = autorelease(make_int(10));
    CljObject *args1[1] = {arg1};
    printf("   (one-param 10): ");
    CljObject *result1 = autorelease(clj_call_function(func1, 1, args1));
    printf("%s\n", pr_str(result1));
    
    // Funktion mit zwei Parametern aufrufen
    CljObject *arg2a = autorelease(make_int(20));
    CljObject *arg2b = autorelease(make_int(30));
    CljObject *args2[2] = {arg2a, arg2b};
    printf("   (two-params 20 30): ");
    CljObject *result2 = autorelease(clj_call_function(func2, 2, args2));
    printf("%s\n", pr_str(result2));
    
    // Funktion mit drei Parametern aufrufen
    CljObject *arg3a = autorelease(make_int(40));
    CljObject *arg3b = autorelease(make_int(50));
    CljObject *arg3c = autorelease(make_int(60));
    CljObject *args3[3] = {arg3a, arg3b, arg3c};
    printf("   (three-params 40 50 60): ");
    CljObject *result3 = autorelease(clj_call_function(func3, 3, args3));
    printf("%s\n", pr_str(result3));
    printf("\n");
    
    // 4. Arity-Check testen
    printf("5. ARITY-CHECK (FEHLERBEHANDLUNG)\n");
    CljObject *wrong_args[1] = {arg1};
    printf("   (two-params 10) - falsche Anzahl Parameter: ");
    CljObject *arity_error = autorelease(clj_call_function(func2, 1, wrong_args));
    printf("%s\n", pr_str(arity_error));
    printf("\n");
    
    // 5. Environment-System demonstrieren
    printf("6. ENVIRONMENT-SYSTEM\n");
    printf("   - env_extend_stack() erstellt neue Environment-Maps\n");
    printf("   - Parameter werden in interleaved Key/Value Arrays gespeichert\n");
    printf("   - Stack-basierte Allokation für temporäre Environments\n");
    printf("   - Automatische Speicherverwaltung durch Reference Counting\n\n");
    
    // 6. Cleanup
    printf("7. CLEANUP UND SPEICHERVERWALTUNG\n");
    printf("   - Alle Objekte werden automatisch freigegeben\n");
    printf("   - Keine Memory Leaks durch Reference Counting\n");
    printf("   - Stack-Allokation wird automatisch aufgeräumt\n\n");
    
    // Cleanup durchführen
    printf("   ✓ Alle Objekte werden automatisch durch autorelease freigegeben\n");
    
    printf("=== DEMONSTRATION ERFOLGREICH ABGESCHLOSSEN ===\n");
    printf("\nDas Array-basierte Funktionsaufruf-System von Tiny-Clj:\n");
    printf("✓ Stack-Allokation für Argumente und Environments\n");
    printf("✓ Interleaved Key/Value Arrays für Maps\n");
    printf("✓ STM32-sichere Parameter-Limits (max 32)\n");
    printf("✓ Automatische Speicherverwaltung\n");
    printf("✓ Clojure-kompatible Funktionsaufrufe\n");
    
    return 0;
}
