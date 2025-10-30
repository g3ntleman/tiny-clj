#include "vector.h"
#include "parser.h"
#include "reader.h"
#include "memory.h"
#include "clj_strings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

CljObject* history_trim_last_n(CljObject *vec, int limit) {
    if (!vec || !is_type(vec, CLJ_VECTOR) || limit <= 0) return make_vector(0, 0);
    CljPersistentVector *v = as_vector(vec);
    if (!v || v->count <= limit) return RETAIN(vec);
    int start = v->count - limit;
    CljValue out = make_vector(limit, 0);
    CljPersistentVector *o = as_vector((CljObject*)out);
    for (int i = 0; i < limit; i++) {
        o->data[i] = (RETAIN(v->data[start + i]), v->data[start + i]);
    }
    o->count = limit;
    return (CljObject*)out;
}

bool history_save_to_file(CljObject *vec, const char *path) {
    if (!path || !vec || !is_type(vec, CLJ_VECTOR)) return false;
    CljObject *trimmed = history_trim_last_n(vec, 50);
    char *s = pr_str(trimmed);
    FILE *fp = fopen(path, "w");
    if (!fp) { if (s) free(s); RELEASE(trimmed); return false; }
    size_t n = fwrite(s, 1, strlen(s), fp);
    fclose(fp);
    free(s);
    RELEASE(trimmed);
    return n > 0;
}

CljObject* history_load_from_file(const char *path) {
    if (!path) return AUTORELEASE(make_vector(0, 0));
    FILE *fp = fopen(path, "r");
    if (!fp) return AUTORELEASE(make_vector(0, 0));
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return AUTORELEASE(make_vector(0, 0)); }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return AUTORELEASE(make_vector(0, 0)); }
    rewind(fp);
    char *buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return AUTORELEASE(make_vector(0, 0)); }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    buf[n] = '\0';
    fclose(fp);

    CljObject *result = NULL;
    WITH_AUTORELEASE_POOL({
        Reader rd; reader_init(&rd, buf);
        EvalState *st = evalstate();
        TRY {
            ID form = value_by_parsing_expr(&rd, st);
            if (form && is_type((CljObject*)form, CLJ_VECTOR)) {
                CljPersistentVector *v = as_vector((CljObject*)form);
                bool all_strings = true;
                for (int i = 0; i < (v ? v->count : 0); i++) {
                    if (!is_type(v->data[i], CLJ_STRING)) { all_strings = false; break; }
                }
                if (all_strings) {
                    result = RETAIN((CljObject*)form);
                }
            }
        } CATCH(ex) {
            result = NULL;
        } END_TRY
    });
    free(buf);
    if (!result) result = make_vector(0, 0);
    return AUTORELEASE(result);
}
