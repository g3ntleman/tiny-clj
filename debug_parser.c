#include "function_call.h"
#include "object.h"
#include "namespace.h"
#include "memory.h"
#include "value.h"
#include "symbol.h"
#include "runtime.h"
#include "parser.h"
#include <stdio.h>
#include <assert.h>

int main() {
    printf("Starting parser debug...\n");
    fflush(stdout);
    
    printf("About to call evalstate_new()\n");
    fflush(stdout);
    EvalState *st = evalstate_new();
    printf("evalstate_new() returned %p\n", st);
    fflush(stdout);
    if (!st) {
        printf("Failed to create EvalState\n");
        return 1;
    }
    printf("EvalState created\n");
    
    printf("About to call init_special_symbols()\n");
    fflush(stdout);
    init_special_symbols();
    printf("init_special_symbols() completed\n");
    fflush(stdout);
    printf("Special symbols initialized\n");
    printf("SYM_RECUR = %p\n", SYM_RECUR);
    
    // Test simple parsing first
    printf("Testing simple parsing...\n");
    printf("About to call eval_string with (+ 1 2)\n");
    fflush(stdout);
    CljObject *simple = eval_string("(+ 1 2)", st);
    printf("eval_string returned %p\n", simple);
    fflush(stdout);
    if (simple) {
        printf("Simple parsing successful\n");
        RELEASE(simple);
    } else {
        printf("Simple parsing failed\n");
    }
    
    // Test function definition without recur
    printf("Testing function definition without recur...\n");
    printf("About to call eval_string with (def test-fn (fn [x] x))\n");
    fflush(stdout);
    CljObject *fn_def = eval_string("(def test-fn (fn [x] x))", st);
    printf("eval_string returned %p\n", fn_def);
    fflush(stdout);
    if (fn_def) {
        printf("Function definition without recur successful\n");
        RELEASE(fn_def);
    } else {
        printf("Function definition without recur failed\n");
    }
    
    // Test function definition with recur
    printf("Testing function definition with recur...\n");
    printf("About to call eval_string with factorial definition\n");
    fflush(stdout);
    CljObject *recur_def = eval_string("(def factorial (fn [n acc] (if (= n 0) acc (recur (- n 1) (* n acc)))))", st);
    printf("eval_string returned %p\n", recur_def);
    fflush(stdout);
    if (recur_def) {
        printf("Function definition with recur successful\n");
        RELEASE(recur_def);
        
        // Test function call with recur
        printf("Testing function call with recur...\n");
        printf("About to call eval_string with (factorial 3 1)\n");
        fflush(stdout);
        CljObject *result = eval_string("(factorial 3 1)", st);
        printf("eval_string returned %p\n", result);
        fflush(stdout);
        if (result) {
            printf("Function call with recur successful, result = %d\n", as_fixnum((CljValue)result));
            RELEASE(result);
        } else {
            printf("Function call with recur failed\n");
        }
    } else {
        printf("Function definition with recur failed\n");
    }
    
    evalstate_free(st);
    printf("Debug completed\n");
    return 0;
}
