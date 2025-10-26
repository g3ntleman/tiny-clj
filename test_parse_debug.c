#include "parser.h"
#include "runtime.h"
#include "memory.h"

int main() {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        if (!st) {
            printf("Failed to create EvalState\n");
            return 1;
        }
        
        printf("Testing parse with '42'...\n");
        CljValue parsed = parse("42", st);
        if (parsed == NULL) {
            printf("Parse failed - returned NULL\n");
        } else {
            printf("Parse succeeded - got value: %ld\n", (long)parsed);
        }
        
        printf("Testing eval_string with '42'...\n");
        CljObject *result = eval_string("42", st);
        if (result == NULL) {
            printf("eval_string failed - returned NULL\n");
        } else {
            printf("eval_string succeeded - got result\n");
        }
        
        evalstate_free(st);
    });
    return 0;
}
