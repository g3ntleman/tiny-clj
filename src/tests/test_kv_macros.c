#include <stdio.h>
#include <stdlib.h>
#include "../CljObject.h"
#include "../kv_macros.h"
#include "../runtime.h"
#include "../map.h"

// Callback-Funktion für map_foreach
void print_kv(CljObject *key, CljObject *value) {
    printf("     %s -> %s\n", pr_str(key), pr_str(value));
}

int main() {
    printf("=== KV-Makros Demonstration ===\n\n");
    
    // Symbol-Tabelle initialisieren
    symbol_table_cleanup();
    
    printf("1. MAP-ERSTELLUNG MIT KV-MAKROS\n");
    CljObject *map = make_map(4);
    printf("   Map erstellt mit Kapazität 4\n\n");
    
    printf("2. SYMBOLE ERSTELLEN\n");
    CljObject *key1 = intern_symbol_global("name");
    CljObject *key2 = intern_symbol_global("age");
    CljObject *key3 = intern_symbol_global("city");
    
    CljObject *val1 = make_string("Alice");
    CljObject *val2 = make_int(25);
    CljObject *val3 = make_string("Berlin");
    
    printf("   Keys: %s, %s, %s\n", pr_str(key1), pr_str(key2), pr_str(key3));
    printf("   Values: %s, %s, %s\n", pr_str(val1), pr_str(val2), pr_str(val3));
    printf("\n");
    
    printf("3. MAP-OPERATIONEN MIT KV-MAKROS\n");
    
    // map_assoc verwendet jetzt KV-Makros intern
    map_assoc(map, key1, val1);
    map_assoc(map, key2, val2);
    map_assoc(map, key3, val3);
    
    printf("   Map nach dem Hinzufügen: %s\n", pr_str(map));
    printf("   Map Count: %d\n\n", map_count(map));
    
    printf("4. MAP-LOOKUP MIT KV-MAKROS\n");
    CljObject *found_val1 = map_get(map, key1);
    CljObject *found_val2 = map_get(map, key2);
    CljObject *found_val3 = map_get(map, key3);
    
    printf("   map_get(map, 'name'): %s\n", pr_str(found_val1));
    printf("   map_get(map, 'age'): %s\n", pr_str(found_val2));
    printf("   map_get(map, 'city'): %s\n", pr_str(found_val3));
    printf("\n");
    
    printf("5. MAP-CONTAINS MIT KV-MAKROS\n");
    printf("   map_contains(map, 'name'): %d\n", map_contains(map, key1));
    printf("   map_contains(map, 'unknown'): %d\n", map_contains(map, intern_symbol_global("unknown")));
    printf("\n");
    
    printf("6. MAP-FOREACH MIT KV-MAKROS\n");
    printf("   Iteriere über alle Key-Value-Paare:\n");
    map_foreach(map, print_kv);
    printf("\n");
    
    printf("7. MAP-KEYS UND MAP-VALS MIT KV-MAKROS\n");
    CljObject *keys = map_keys(map);
    CljObject *vals = map_vals(map);
    
    printf("   Keys: %s\n", pr_str(keys));
    printf("   Values: %s\n", pr_str(vals));
    printf("\n");
    
    printf("8. MAP-REMOVE MIT KV-MAKROS\n");
    printf("   Entferne 'age' aus der Map...\n");
    map_remove(map, key2);
    printf("   Map nach dem Entfernen: %s\n", pr_str(map));
    printf("   Map Count: %d\n\n", map_count(map));
    
    printf("9. DIREKTE KV-MAKRO-NUTZUNG\n");
    printf("   Demonstriere direkte Verwendung der KV-Makros:\n");
    
    // Erstelle ein neues Array für direkte KV-Makro-Nutzung
    CljObject **kv_array = ALLOC(CljObject*, 6); // 3 Paare
    
    // Setze Key-Value-Paare mit KV-Makros
    KV_SET_PAIR(kv_array, 0, key1, val1);
    KV_SET_PAIR(kv_array, 1, key2, val2);
    KV_SET_PAIR(kv_array, 2, key3, val3);
    
    printf("   Direkte KV-Makro-Nutzung:\n");
    for (int i = 0; i < 3; i++) {
        CljObject *k = KV_KEY(kv_array, i);
        CljObject *v = KV_VALUE(kv_array, i);
        printf("     [%d] %s -> %s\n", i, pr_str(k), pr_str(v));
    }
    
    printf("   KV_CONTAINS(kv_array, 3, key1): %d\n", KV_CONTAINS(kv_array, 3, key1));
    printf("   KV_FIND_INDEX(kv_array, 3, key2): %d\n", KV_FIND_INDEX(kv_array, 3, key2));
    printf("   KV_COUNT_VALID(kv_array, 3): %d\n", KV_COUNT_VALID(kv_array, 3));
    printf("\n");
    
    printf("10. CLEANUP\n");
    free(kv_array);
    release(map);
    release(keys);
    release(vals);
    release(key1);
    release(key2);
    release(key3);
    release(val1);
    release(val2);
    release(val3);
    
    printf("=== DEMONSTRATION ERFOLGREICH ABGESCHLOSSEN ===\n");
    printf("\nDie KV-Makros bieten:\n");
    printf("✓ Saubere, lesbare Syntax für Key-Value-Operationen\n");
    printf("✓ Typsichere Zugriffe auf interleaved Arrays\n");
    printf("✓ Wiederverwendbare Makros für verschiedene Datenstrukturen\n");
    printf("✓ Konsistente API für alle Map-Operationen\n");
    printf("✓ Bessere Wartbarkeit und Fehlervermeidung\n");
    
    return 0;
}
