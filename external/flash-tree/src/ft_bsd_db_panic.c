#include <stdlib.h>

#include "ft_bsd_db.h"

void __dbpanic(DB *dbp) {
    (void)dbp;
    abort();
}

