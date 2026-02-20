#include <stdlib.h>

#include "tdb_bsd_db.h"

/**
 * @brief __dbpanic.
 * @param dbp B-Tree database handle.
 */
void __dbpanic(DB* dbp) {
    (void)dbp;
    abort();
}
