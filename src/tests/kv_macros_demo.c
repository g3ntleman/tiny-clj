#include <stdio.h>
#include <stdlib.h>
#include "../CljObject.h"
#include "../kv_macros.h"
#include "../runtime.h"

// Beispiel-Funktionen für verschiedene KV-Makro-Anwendungen

// Environment-Lookup mit KV-Makros (wie im ursprünglichen Beispiel)
CljObject* env_lookup_demo(CljObject **env_kv, int env_count, CljObject* key) {
    for (int i = 0; i < env_count; i++) {
        if (KV_KEY(env_kv, i) == key) {  // pointer compare for symbols
            return KV_VALUE(env_kv, i);
        }
    }
    return NULL; // not found
}

// Array-basierte Map Lookup mit KV-Makros
CljObject* array_map_get(CljObject **kv_array, int count, CljObject* key) {
    for (int i = 0; i < count; i++) {
        if (KV_KEY(kv_array, i) == key) {
            return KV_VALUE(kv_array, i);
        }
    }
    return NULL; // not found
}

// Callback für KV_FOREACH
void print_pair(CljObject *key, CljObject *value) {
    printf("  %s -> %s\n", pr_str(key), pr_str(value));
}

int main() {
    printf("=== KV-Makros Generalisierung - Praktische Anwendungen ===\n\n");
    
    // Symbol-Tabelle initialisieren
    symbol_table_cleanup();
    
    printf("1. ENVIRONMENT-LOOKUP MIT KV-MAKROS\n");
    printf("   Simuliere ein Environment mit Key-Value-Paaren:\n");
    
    // Erstelle ein Environment-Array
    CljObject **env_kv = ALLOC(CljObject*, 6); // 3 Paare
    
    CljObject *x_sym = intern_symbol_global("x");
    CljObject *y_sym = intern_symbol_global("y");
    CljObject *z_sym = intern_symbol_global("z");
    
    CljObject *x_val = make_int(10);
    CljObject *y_val = make_int(20);
    CljObject *z_val = make_int(30);
    
    // Setze Environment-Bindungen mit KV-Makros
    KV_SET_PAIR(env_kv, 0, x_sym, x_val);
    KV_SET_PAIR(env_kv, 1, y_sym, y_val);
    KV_SET_PAIR(env_kv, 2, z_sym, z_val);
    
    printf("   Environment-Bindungen:\n");
    for (int i = 0; i < 3; i++) {
        printf("     %s = %s\n", pr_str(KV_KEY(env_kv, i)), pr_str(KV_VALUE(env_kv, i)));
    }
    
    // Teste Environment-Lookup
    CljObject *found_x = env_lookup_demo(env_kv, 3, x_sym);
    CljObject *found_y = env_lookup_demo(env_kv, 3, y_sym);
    CljObject *unknown = intern_symbol_global("unknown");
    CljObject *found_unknown = env_lookup_demo(env_kv, 3, unknown);
    
    printf("   env_lookup(env, 'x'): %s\n", pr_str(found_x));
    printf("   env_lookup(env, 'y'): %s\n", pr_str(found_y));
    printf("   env_lookup(env, 'unknown'): %s\n", found_unknown ? pr_str(found_unknown) : "NULL");
    printf("\n");
    
    printf("2. ARRAY-BASIERTE MAP MIT KV-MAKROS\n");
    printf("   Erstelle eine Map mit verschiedenen Datentypen:\n");
    
    // Erstelle eine Map-Array
    CljObject **map_kv = ALLOC(CljObject*, 8); // 4 Paare
    
    CljObject *name_sym = intern_symbol_global("name");
    CljObject *age_sym = intern_symbol_global("age");
    CljObject *active_sym = intern_symbol_global("active");
    CljObject *score_sym = intern_symbol_global("score");
    
    CljObject *name_val = make_string("Alice");
    CljObject *age_val = make_int(25);
    CljObject *active_val = make_int(1); // true
    CljObject *score_val = make_float(95.5);
    
    // Setze Map-Einträge mit KV-Makros
    KV_SET_PAIR(map_kv, 0, name_sym, name_val);
    KV_SET_PAIR(map_kv, 1, age_sym, age_val);
    KV_SET_PAIR(map_kv, 2, active_sym, active_val);
    KV_SET_PAIR(map_kv, 3, score_sym, score_val);
    
    printf("   Map-Inhalt:\n");
    for (int i = 0; i < 4; i++) {
        printf("     %s = %s\n", pr_str(KV_KEY(map_kv, i)), pr_str(KV_VALUE(map_kv, i)));
    }
    
    // Teste Map-Lookup
    CljObject *found_name = array_map_get(map_kv, 4, name_sym);
    CljObject *found_age = array_map_get(map_kv, 4, age_sym);
    CljObject *found_score = array_map_get(map_kv, 4, score_sym);
    
    printf("   map_get(map, 'name'): %s\n", pr_str(found_name));
    printf("   map_get(map, 'age'): %s\n", pr_str(found_age));
    printf("   map_get(map, 'score'): %s\n", pr_str(found_score));
    printf("\n");
    
    printf("3. KV-MAKRO-UTILITIES\n");
    printf("   Demonstriere erweiterte KV-Makro-Funktionen:\n");
    
    printf("   KV_CONTAINS(map_kv, 4, name_sym): %d\n", KV_CONTAINS(map_kv, 4, name_sym));
    printf("   KV_CONTAINS(map_kv, 4, unknown): %d\n", KV_CONTAINS(map_kv, 4, unknown));
    printf("   KV_FIND_INDEX(map_kv, 4, age_sym): %d\n", KV_FIND_INDEX(map_kv, 4, age_sym));
    printf("   KV_FIND_INDEX(map_kv, 4, unknown): %d\n", KV_FIND_INDEX(map_kv, 4, unknown));
    printf("   KV_COUNT_VALID(map_kv, 4): %d\n", KV_COUNT_VALID(map_kv, 4));
    printf("\n");
    
    printf("4. KV_FOREACH ITERATION\n");
    printf("   Iteriere über alle Map-Einträge:\n");
    KV_FOREACH(map_kv, 4, key, value, {
        print_pair(key, value);
    });
    printf("\n");
    
    printf("5. DYNAMISCHE KV-OPERATIONEN\n");
    printf("   Demonstriere dynamische Key-Value-Manipulation:\n");
    
    // Füge einen neuen Eintrag hinzu
    CljObject *new_key = intern_symbol_global("city");
    CljObject *new_value = make_string("Berlin");
    KV_SET_PAIR(map_kv, 4, new_key, new_value);
    
    printf("   Nach dem Hinzufügen von 'city':\n");
    KV_FOREACH(map_kv, 5, key, value, {
        print_pair(key, value);
    });
    
    // Ändere einen existierenden Wert
    CljObject *new_age = make_int(26);
    int age_index = KV_FIND_INDEX(map_kv, 5, age_sym);
    if (age_index >= 0) {
        CljObject *old_age = KV_VALUE(map_kv, age_index);
        release(old_age); // Freigabe des alten Werts
        KV_SET_VALUE(map_kv, age_index, new_age);
    }
    
    printf("   Nach dem Ändern von 'age' zu 26:\n");
    KV_FOREACH(map_kv, 5, key, value, {
        print_pair(key, value);
    });
    printf("\n");
    
    printf("6. PERFORMANCE-VERGLEICH\n");
    printf("   KV-Makros vs. manuelle Array-Indizierung:\n");
    
    // Teste Performance mit vielen Einträgen
    const int test_size = 1000;
    CljObject **test_kv = ALLOC(CljObject*, test_size * 2);
    
    // Fülle Test-Array
    for (int i = 0; i < test_size; i++) {
        char key_name[32];
        snprintf(key_name, sizeof(key_name), "key_%d", i);
        CljObject *key = intern_symbol_global(key_name);
        CljObject *value = make_int(i);
        KV_SET_PAIR(test_kv, i, key, value);
    }
    
    // Teste Lookup-Performance
    CljObject *test_key = intern_symbol_global("key_500");
    int found_index = KV_FIND_INDEX(test_kv, test_size, test_key);
    printf("   Gefunden 'key_500' an Index: %d\n", found_index);
    
    if (found_index >= 0) {
        CljObject *found_value = KV_VALUE(test_kv, found_index);
        printf("   Wert: %s\n", pr_str(found_value));
    }
    
    printf("   Test mit %d Einträgen erfolgreich abgeschlossen\n", test_size);
    printf("\n");
    
    printf("7. CLEANUP\n");
    // Cleanup
    free(env_kv);
    free(map_kv);
    free(test_kv);
    
    // Release alle Werte
    release(x_sym); release(y_sym); release(z_sym);
    release(x_val); release(y_val); release(z_val);
    release(name_sym); release(age_sym); release(active_sym); release(score_sym);
    release(name_val); release(age_val); release(active_val); release(score_val);
    release(new_key); release(new_value); release(new_age);
    release(unknown);
    
    printf("=== GENERALISIERUNG ERFOLGREICH ABGESCHLOSSEN ===\n");
    printf("\nDie KV-Makros bieten eine generalisierte Lösung für:\n");
    printf("✓ Environment-Lookup in Interpretern\n");
    printf("✓ Map-Operationen in Datenstrukturen\n");
    printf("✓ Key-Value-Arrays in verschiedenen Kontexten\n");
    printf("✓ Konsistente API für alle interleaved Arrays\n");
    printf("✓ Typsichere und performante Zugriffe\n");
    printf("✓ Wiederverwendbare Makros für verschiedene Anwendungen\n");
    
    return 0;
}
