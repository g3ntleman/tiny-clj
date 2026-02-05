# Dead Code Audit: Candidates

Heuristics:
- **Linker-stripped** (Release `-dead_strip`) symbols are unreachable from the chosen entrypoints.
- **Never-executed** (coverage) functions were not hit by the executed test subset.
- The **intersection** is the highest-confidence dead-code candidate set.

## `tiny-clj-repl`

- Linker-stripped candidates: **191**
- Coverage never-called (core): **332**
- **High-confidence (intersection)**: **83**

### High-confidence candidates (first 200)

- `allocate_args_array`
- `ast_compile_expr_inplace`
- `ast_compile_inplace`
- `ast_node_clear_callsite_cache`
- `ast_node_get_callsite_cache`
- `atom_reset`
- `cleanup_args_array`
- `clj_ftoa`
- `clj_uitoa`
- `clojure_core_set_source`
- `compare_numeric_values`
- `conj2_wrapper`
- `eval_arg`
- `eval_body_with_env`
- `eval_catch`
- `eval_compiled_call`
- `eval_compiled_do`
- `eval_compiled_fn`
- `eval_compiled_if`
- `eval_compiled_let`
- `eval_error`
- `eval_list_function`
- `eval_seq`
- `eval_set_use_compiled_ast`
- `eval_special_dotimes`
- `eval_special_form_dispatch`
- `eval_try`
- `extract_numeric_values`
- `fs_exists`
- `fs_file_stream_read`
- `fs_file_stream_read_from`
- `fs_file_stream_write`
- `fs_global_store`
- `fs_global_store_reset`
- `fs_kv_del`
- `fs_kv_del_key_bytes_status`
- `fs_kv_del_status`
- `fs_kv_get`
- `fs_kv_get_status`
- `fs_kv_put`
- `fs_kv_put_status`
- `fs_kv_store_free`
- `fs_kv_stream_read_key_bytes`
- `fs_kv_stream_read_key_bytes_from`
- `fs_kv_stream_write_key_bytes`
- `fs_make_chunk_key`
- `fs_meta_put`
- `fs_stream_stats_get`
- `fs_stream_stats_reset`
- `is_earmuffed_dynamic_symbol`
- `is_seq`
- `is_zombie`
- `make_symbol_token`
- `make_symbol_token_with_loc`
- `mdns_decode_name`
- `mdns_encode_qname`
- `mdns_resolver_init`
- `mdns_resolver_start_browse`
- `mdns_resolver_storage_size`
- `mdns_resolver_tick`
- `meta_merge_with_precedence`
- `ns_load_file`
- `parse_error`
- `parser_set_disable_meta`
- `print_str`
- `reader_get_source_name`
- `reader_init_with_source`
- `reader_is_delimiter`
- `reader_is_symbol_char`
- `reader_skip_all_including_newlines`
- `reader_skip_whitespace_including_newlines`
- `reference_count`
- `seq_iter_position`
- `should_suggest_require_for_ns`
- `strings_clear_special_forms`
- `strings_get_special_form_rendering`
- `strings_register_special_form`
- `strings_set_special_form_rendering`
- `symbol_get_namespace_name`
- `throw_index_out_of_bounds`
- `validate_arity`
- `validate_min_arity`
- `validate_recur_positions`

## `unit-tests`

- Linker-stripped candidates: **98**
- Coverage never-called (core): **332**
- **High-confidence (intersection)**: **47**

### High-confidence candidates (first 200)

- `allocate_args_array`
- `ast_compile_expr_inplace`
- `ast_compile_inplace`
- `ast_compile_list_inplace`
- `ast_compile_map_inplace`
- `ast_compile_vector_inplace`
- `ast_node_clear_callsite_cache`
- `ast_node_get_callsite_cache`
- `cleanup_args_array`
- `clj_uitoa`
- `clojure_core_set_source`
- `conj2_wrapper`
- `eval_arg`
- `eval_catch`
- `eval_compiled_call`
- `eval_compiled_do`
- `eval_compiled_fn`
- `eval_compiled_if`
- `eval_compiled_let`
- `eval_list_function`
- `eval_seq`
- `eval_special_dotimes`
- `eval_special_form_dispatch`
- `eval_try`
- `is_seq`
- `is_zombie`
- `make_pseudo_list`
- `make_symbol_token`
- `make_symbol_token_with_loc`
- `meta_merge_with_precedence`
- `ns_load_file`
- `parse_error`
- `parser_set_disable_meta`
- `reader_check_codepoint_property`
- `reader_is_delimiter`
- `reader_is_symbol_char`
- `reader_skip_all_including_newlines`
- `reader_skip_whitespace_including_newlines`
- `reference_count`
- `seq_iter_position`
- `strings_clear_special_forms`
- `strings_get_special_form_rendering`
- `strings_register_special_form`
- `sym_cname_eq`
- `symbol_get_namespace_name`
- `validate_min_arity`
- `validate_recur_positions`

