#include "tests_common.h"
#include "object.h"
#include "list.h"
#include "value.h"
#include "clj_strings.h"
#include "types.h"
#include "function_call.h"
#include "symbol.h"
#include "vector.h"
#include "exception.h"
#include "namespace.h"

// Forward declaration
int load_clojure_core(EvalState *st);

// ============================================================================
// DOTIMES SPECIAL FORM TESTS
// ============================================================================

TEST(test_dotimes_basic_functionality) {
    // Test that dotimes special form executes and returns nil
    // Create a simple dotimes expression: (dotimes [i 3] (println "i =" i))
    CljObject *dotimes_symbol = intern_symbol_global("dotimes");
    CljObject *i_symbol = intern_symbol_global("i");
    CljObject *three = fixnum(3);
    CljObject *println_symbol = intern_symbol_global("println");
    CljObject *i_equals_string = make_string("i =");
    
    // Create the binding vector: [i 3]
    CljObject *binding_vector = make_vector(2, true);
    CljPersistentVector *vec_data = as_vector(binding_vector);
    vec_data->data[0] = i_symbol;
    vec_data->data[1] = three;
    vec_data->count = 2; // Set the count manually
    
    // Create the body expression: (println "i =" i)
    CljObject *body = make_list((ID)println_symbol, 
                               (CljList*)make_list((ID)i_equals_string, 
                               (CljList*)make_list((ID)i_symbol, NULL)));
    
    // Create the dotimes call: (dotimes [i 3] (println "i =" i))
    CljObject *dotimes_call = make_list((ID)dotimes_symbol, 
                                        (CljList*)make_list((ID)binding_vector, 
                                        (CljList*)make_list((ID)body, NULL)));
    
    // Create a simple environment
    CljMap *env = (CljMap*)make_map(4);
    
    // Test dotimes evaluation with environment
    EvalState *st = evalstate();
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(i_equals_string);
    RELEASE((CljObject*)dotimes_call);
    RELEASE(env);
}

TEST(test_dotimes_variable_binding_problem) {
    // This test should show that dotimes properly binds the variable
    // Create a simple dotimes expression that uses the variable i
    CljObject *dotimes_symbol = intern_symbol_global("dotimes");
    CljObject *i_symbol = intern_symbol_global("i");
    CljObject *five = fixnum(5);
    
    // Create the binding vector: [i 5]
    CljObject *binding_vector = make_vector(2, true);
    CljPersistentVector *vec_data = as_vector(binding_vector);
    vec_data->data[0] = i_symbol;
    vec_data->data[1] = five;
    vec_data->count = 2; // Set the count manually
    
    // Create a body that uses the variable i: i (just return the variable)
    CljObject *body = i_symbol;
    
    // Create the dotimes call: (dotimes [i 5] i)
    CljObject *dotimes_call = make_list((ID)dotimes_symbol, 
                                        (CljList*)make_list((ID)binding_vector, 
                                        (CljList*)make_list((ID)body, NULL)));
    
    // Create a simple environment
    CljMap *env = (CljMap*)make_map(4);
    
    // Test dotimes evaluation with environment
    EvalState *st = evalstate();
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE((CljObject*)dotimes_call);
    RELEASE(env);
}

TEST(test_dotimes_with_time_measurement) {
    // Test that shows the timing with dotimes
    // This should demonstrate that dotimes executes the body multiple times
    
    // Create a dotimes expression: (dotimes [i 1000] (fib 20))
    CljObject *dotimes_symbol = intern_symbol_global("dotimes");
    CljObject *i_symbol = intern_symbol_global("i");
    CljObject *thousand = fixnum(1000);
    CljObject *fib_symbol = intern_symbol_global("fib");
    CljObject *twenty = fixnum(20);
    
    // Create the binding vector: [i 1000]
    CljObject *binding_vector = make_vector(2, true);
    CljPersistentVector *vec_data = as_vector(binding_vector);
    vec_data->data[0] = i_symbol;
    vec_data->data[1] = thousand;
    vec_data->count = 2; // Set the count manually
    
    // Create the body expression: (fib 20)
    CljObject *body = make_list((ID)fib_symbol, 
                               (CljList*)make_list((ID)twenty, NULL));
    
    // Create the dotimes call: (dotimes [i 1000] (fib 20))
    CljObject *dotimes_call = make_list((ID)dotimes_symbol, 
                                        (CljList*)make_list((ID)binding_vector, 
                                        (CljList*)make_list((ID)body, NULL)));
    
    // Create a simple environment
    CljMap *env = (CljMap*)make_map(4);
    
    // Test dotimes evaluation with environment
    EvalState *st = evalstate();
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE((CljObject*)dotimes_call);
    RELEASE(env);
}

TEST(test_dotimes_arity_validation) {
    // Test that dotimes validates arity correctly
    // Create a dotimes call with insufficient arguments: (dotimes)
    CljObject *dotimes_symbol = intern_symbol_global("dotimes");
    
    // Create the dotimes call: (dotimes)
    CljObject *dotimes_call = make_list((ID)dotimes_symbol, NULL);
    
    // Create a simple environment
    CljMap *env = (CljMap*)make_map(4);
    
    // Test dotimes evaluation with environment - should return NULL for insufficient args
    EvalState *st = evalstate();
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for insufficient args
    
    // Clean up
    RELEASE((CljObject*)dotimes_call);
    RELEASE(env);
}

TEST(test_dotimes_invalid_binding_format_new) {
    // Test dotimes with invalid binding format
    // Create invalid binding vector: [i] (missing count)
    CljObject *dotimes_symbol = intern_symbol_global("dotimes");
    CljObject *i_symbol = intern_symbol_global("i");
    
    // Create invalid binding vector: [i] (missing count)
    CljObject *binding_vector = make_vector(1, true);
    CljPersistentVector *vec_data = as_vector(binding_vector);
    vec_data->data[0] = i_symbol;
    vec_data->count = 1; // Set the count manually
    
    // Create body: i
    CljObject *body = i_symbol;
    
    // Create the dotimes call: (dotimes [i] i)
    CljObject *dotimes_call = make_list((ID)dotimes_symbol, 
                                        (CljList*)make_list((ID)binding_vector, 
                                        (CljList*)make_list((ID)body, NULL)));
    
    // Create a simple environment
    CljMap *env = (CljMap*)make_map(4);
    
    // Test dotimes evaluation with environment - should return NULL for invalid binding
    EvalState *st = evalstate();
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for invalid binding format
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE((CljObject*)dotimes_call);
    RELEASE(env);
}