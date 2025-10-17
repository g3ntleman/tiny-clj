int main() { EvalState *st = evalstate_new(); register_builtins(); CljObject *result = eval_string("42", st); printf("Result: %p, type: %d
", result, result ? result->type : -1); evalstate_free(st); return 0; }
