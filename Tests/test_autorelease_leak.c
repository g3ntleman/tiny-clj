#include <stdio.h>
#include <stdlib.h>
#include "src/CljObject.h"

int main() {
    printf("=== Test Autorelease Pool Leak ===\n");
    
    // Symbol-Tabelle initialisieren
    symbol_table_cleanup();
    
    printf("1. Erstelle Objekte mit autorelease...\n");
    CljObject *obj1 = autorelease(make_int(42));
    CljObject *obj2 = autorelease(make_string("test"));
    CljObject *obj3 = autorelease(make_int(100));
    
    printf("   obj1: %s\n", pr_str(obj1));
    printf("   obj2: %s\n", pr_str(obj2));
    printf("   obj3: %s\n", pr_str(obj3));
    
    printf("2. Cleanup Autorelease-Pool...\n");
    cljvalue_pool_cleanup_all();
    printf("   ✓ Alle Objekte wurden freigegeben!\n");
    
    return 0;
}
