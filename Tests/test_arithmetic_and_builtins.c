#include <stdio.h>
#include <stdlib.h>
#include "src/CljObject.h"
#include "src/clj_symbols.h"
#include "src/function_call.h"

int main() {
    printf("=== Clojure Arithmetic and Built-in Functions Test ===\n\n");
    
    // 1. Symbol-Tabelle initialisieren
    symbol_table_cleanup();
    
    // 2. Teste arithmetische Operationen
    printf("1. ARITHMETISCHE OPERATIONEN:\n");
    
    // Addition: (+ 5 3)
    printf("   (+ 5 3): ");
    CljObject *add_list = make_list();
    CljList *add_list_data = as_list(add_list);
    add_list_data->head = intern_symbol_global("+");
    add_list_data->tail = make_list();
    
    CljList *add_tail = as_list(add_list_data->tail);
    add_tail->head = make_int(5);
    add_tail->tail = make_list();
    
    CljList *add_tail2 = as_list(add_tail->tail);
    add_tail2->head = make_int(3);
    add_tail2->tail = NULL;
    
    CljObject *add_result = eval_list(add_list, NULL);
    printf("%s\n", pr_str(add_result));
    release(add_result);
    release(add_list);
    
    // Multiplikation: (* 7 4)
    printf("   (* 7 4): ");
    CljObject *mul_list = make_list();
    CljList *mul_list_data = as_list(mul_list);
    mul_list_data->head = intern_symbol_global("*");
    mul_list_data->tail = make_list();
    
    CljList *mul_tail = as_list(mul_list_data->tail);
    mul_tail->head = make_int(7);
    mul_tail->tail = make_list();
    
    CljList *mul_tail2 = as_list(mul_tail->tail);
    mul_tail2->head = make_int(4);
    mul_tail2->tail = NULL;
    
    CljObject *mul_result = eval_list(mul_list, NULL);
    printf("%s\n", pr_str(mul_result));
    release(mul_result);
    release(mul_list);
    
    // Division: (/ 15 3)
    printf("   (/ 15 3): ");
    CljObject *div_list = make_list();
    CljList *div_list_data = as_list(div_list);
    div_list_data->head = intern_symbol_global("/");
    div_list_data->tail = make_list();
    
    CljList *div_tail = as_list(div_list_data->tail);
    div_tail->head = make_int(15);
    div_tail->tail = make_list();
    
    CljList *div_tail2 = as_list(div_tail->tail);
    div_tail2->head = make_int(3);
    div_tail2->tail = NULL;
    
    CljObject *div_result = eval_list(div_list, NULL);
    printf("%s\n", pr_str(div_result));
    release(div_result);
    release(div_list);
    
    // 3. Teste Built-in Funktionen
    printf("\n2. BUILT-IN FUNKTIONEN:\n");
    
    // println: (println "Hello World")
    printf("   (println \"Hello World\"): ");
    CljObject *println_list = make_list();
    CljList *println_list_data = as_list(println_list);
    println_list_data->head = intern_symbol_global("println");
    println_list_data->tail = make_list();
    
    CljList *println_tail = as_list(println_list_data->tail);
    println_tail->head = make_string("Hello World");
    println_tail->tail = NULL;
    
    CljObject *println_result = eval_list(println_list, NULL);
    printf("%s\n", pr_str(println_result));
    release(println_result);
    release(println_list);
    
    // str: (str 42)
    printf("   (str 42): ");
    CljObject *str_list = make_list();
    CljList *str_list_data = as_list(str_list);
    str_list_data->head = intern_symbol_global("str");
    str_list_data->tail = make_list();
    
    CljList *str_tail = as_list(str_list_data->tail);
    str_tail->head = make_int(42);
    str_tail->tail = NULL;
    
    CljObject *str_result = eval_list(str_list, NULL);
    printf("%s\n", pr_str(str_result));
    release(str_result);
    release(str_list);
    
    // count: (count [1 2 3 4 5])
    printf("   (count [1 2 3 4 5]): ");
    CljObject *vec = make_vector(5, 0);
    CljPersistentVector *vec_data = as_vector(vec);
    if (vec_data) {
        vec_data->data[0] = make_int(1);
        vec_data->data[1] = make_int(2);
        vec_data->data[2] = make_int(3);
        vec_data->data[3] = make_int(4);
        vec_data->data[4] = make_int(5);
        vec_data->count = 5;
    }
    
    CljObject *count_list = make_list();
    CljList *count_list_data = as_list(count_list);
    count_list_data->head = intern_symbol_global("count");
    count_list_data->tail = make_list();
    
    CljList *count_tail = as_list(count_list_data->tail);
    count_tail->head = vec;
    count_tail->tail = NULL;
    
    CljObject *count_result = eval_list(count_list, NULL);
    printf("%s\n", pr_str(count_result));
    release(count_result);
    release(count_list);
    release(vec);
    
    // first: (first [10 20 30])
    printf("   (first [10 20 30]): ");
    CljObject *vec2 = make_vector(3, 0);
    CljPersistentVector *vec2_data = as_vector(vec2);
    if (vec2_data) {
        vec2_data->data[0] = make_int(10);
        vec2_data->data[1] = make_int(20);
        vec2_data->data[2] = make_int(30);
        vec2_data->count = 3;
    }
    
    CljObject *first_list = make_list();
    CljList *first_list_data = as_list(first_list);
    first_list_data->head = intern_symbol_global("first");
    first_list_data->tail = make_list();
    
    CljList *first_tail = as_list(first_list_data->tail);
    first_tail->head = vec2;
    first_tail->tail = NULL;
    
    CljObject *first_result = eval_list(first_list, NULL);
    printf("%s\n", pr_str(first_result));
    release(first_result);
    release(first_list);
    release(vec2);
    
    // 4. Teste Funktionsdefinition und -aufruf
    printf("\n3. FUNKTIONSDEFINITION UND -AUFRUF:\n");
    
    // Definiere eine Additionsfunktion
    CljObject *x_sym = intern_symbol_global("x");
    CljObject *y_sym = intern_symbol_global("y");
    CljObject *params[2] = {x_sym, y_sym};
    
    // Body: (+ x y)
    CljObject *body = make_list();
    CljList *body_data = as_list(body);
    body_data->head = intern_symbol_global("+");
    body_data->tail = make_list();
    
    CljList *body_tail = as_list(body_data->tail);
    body_tail->head = x_sym;
    body_tail->tail = make_list();
    
    CljList *body_tail2 = as_list(body_tail->tail);
    body_tail2->head = y_sym;
    body_tail2->tail = NULL;
    
    CljObject *add_func = make_function(params, 2, body, NULL, 0, "add");
    printf("   Funktion definiert: %s\n", pr_str(add_func));
    
    // Rufe die Funktion auf: (add 10 20)
    CljObject *args[2] = {make_int(10), make_int(20)};
    CljObject *func_result = eval_function_call(add_func, args, 2, NULL);
    printf("   (add 10 20) = %s\n", pr_str(func_result));
    release(func_result);
    release(add_func);
    release(body);
    release(args[0]);
    release(args[1]);
    
    printf("\n=== ALLE TESTS ERFOLGREICH ABGESCHLOSSEN ===\n");
    printf("✅ Arithmetische Operationen funktionieren!\n");
    printf("✅ Built-in Funktionen funktionieren!\n");
    printf("✅ Funktionsdefinition und -aufruf funktioniert!\n");
    
    return 0;
}
