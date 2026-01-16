#include <stdlib.h>

#include "tdb_bsd_db.h"

void __dbpanic(DB* dbp) {
    (void)dbp;
    abort();
}
