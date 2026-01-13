#include <stdlib.h>

#include "db.h"

void __dbpanic(DB *dbp) {
    (void)dbp;
    abort();
}

