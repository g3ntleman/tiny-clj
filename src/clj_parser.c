/*
 * Clojure Parser Implementation
 * 
 * Features:
 * - Parses Clojure-like syntax (lists, vectors, maps, symbols, keywords, numbers, strings)
 * - Supports meta-data parsing (^metadata, #^{...}, (with-meta obj meta))
 * - Handles comments (line comments ; and block comments #| ... |#)
 * - Stack-allocated parsing for memory efficiency
 * - STM32-compatible implementation
 */

#include "clj_parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "runtime.h"
#include "vector.h"
#include "map.h"
#include "utf8.h"

// Global jump buffer for parse errors
static jmp_buf parse_jmp;

// Error handling - using namespace.h version

// Utility functions
void skip_whitespace(const char **input) {
    while (**input && isspace(**input)) {
        {
            // Handle line counting if needed
        }
        (*input)++;
    }
}

// Skip comments (both line comments and block comments)
void skip_comments(const char **input) {
    while (**input) {
        if (**input == ';') {
            // Line comment - skip until end of line
            while (**input && **input != '\n') {
                (*input)++;
            }
        } else if (**input == '#') {
            // Check for block comment #| ... |# (guard second char)
            if ((*input)[1] && (*input)[1] == '|') {
                (*input) += 2; // Skip #|
                int depth = 1;
                while (**input && depth > 0) {
                    if (**input == '#' && (*input)[1] && (*input)[1] == '|') {
                        depth++;
                        (*input) += 2;
                    } else if (**input == '|' && (*input)[1] && (*input)[1] == '#') {
                        depth--;
                        (*input) += 2;
                    } else {
                        (*input)++;
                    }
                }
            } else {
                // Not a comment, break
                break;
            }
        } else if (isspace(**input)) {
            // Skip whitespace
            (*input)++;
        } else {
            // Not a comment or whitespace, break
            break;
        }
    }
}

int is_whitespace(char c) {
    return isspace(c);
}

int is_digit(char c) {
    return c >= '0' && c <= '9';
}

int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int is_alphanumeric(char c) {
    return is_alpha(c) || is_digit(c) || c == '-' || c == '_' || c == '?' || c == '!' || c == '/';
}

static int is_opchar(char c) {
    return (c == '+') || (c == '*') || (c == '/') || (c == '=') || (c == '<') || (c == '>');
}

// Stack-based vector parsing (minimal heap usage)
CljObject* parse_vector(const char **input, EvalState *st) {
    CljObject *stack[MAX_STACK_VECTOR_SIZE];  // Stack-allocated array
    int count = 0;

    (*input)++; // Skip '['
    skip_whitespace(input);
    skip_comments(input);
    
    while (**input && **input != ']') {
        {
            // Parse error occurred
        }
        stack[count++] = parse_expr(input, st);
        skip_whitespace(input);
        skip_comments(input);
    }

    {
        // Parse error occurred
    }
    (*input)++; // Skip ']'

    // Create heap object from stack array
    return vector_from_stack(stack, count);
}

// Stack-based map parsing
CljObject* parse_map(const char **input, EvalState *st) {
    CljObject *pairs[MAX_STACK_MAP_PAIRS * 2];  // Stack-allocated array for key-value pairs
    int pair_count = 0;

    (*input)++; // Skip '{'
    skip_whitespace(input);
    skip_comments(input);
    
    while (**input && **input != '}') {
        
        // Parse key
        CljObject *key = parse_expr(input, st);
        skip_whitespace(input);
        skip_comments(input);
        
        // Parse value
        CljObject *value = parse_expr(input, st);
        skip_whitespace(input);
        skip_comments(input);
        
        // Store as pair (key, value)
        pairs[pair_count * 2] = key;
        pairs[pair_count * 2 + 1] = value;
        pair_count++;
    }

    {
        // Parse error occurred
    }
    (*input)++; // Skip '}'

    // Create heap object from stack array
    return map_from_stack(pairs, pair_count);
}

// Stack-based list parsing
CljObject* parse_list(const char **input, EvalState *st) {
    CljObject *stack[MAX_STACK_LIST_SIZE];  // Stack-allocated array
    int count = 0;

    (*input)++; // Skip '('
    skip_whitespace(input);
    skip_comments(input);
    
    while (**input && **input != ')') {
        CljObject *expr = parse_expr(input, st);
        if (!expr) {
            // Parse error occurred - skip character and continue
            (*input)++;
            continue;
        }
        stack[count++] = expr;
        skip_whitespace(input);
        skip_comments(input);
    }

    if (**input != ')') {
        // Parse error occurred
    }
    (*input)++; // Skip ')'

    // Create linked list from stack array
    return list_from_stack(stack, count);
}

// Symbol parsing
CljObject* parse_symbol(const char **input, EvalState *st) {
    const char *start = *input;
    
    // Handle keywords
    if (**input == ':') {
        (*input)++; // Skip ':'
        
        // Check for :: (auto-resolved keyword)
        if (**input == ':') {
            (*input)++; // Skip second ':'
            
            // Parse the keyword name
            const char *name_start = *input;
            while (**input && (is_alphanumeric(**input) || **input == '-' || **input == '_')) {
                (*input)++;
            }
            int name_len = *input - name_start;
            
            if (name_len == 0) {
                printf("Parse error: Empty auto-resolved keyword\n");
                return NULL;
            }
            
            // Create auto-resolved keyword: ::keyword -> :current-ns/keyword
            char *name = ALLOC(char, name_len + 1);
            strncpy(name, name_start, name_len);
            name[name_len] = '\0';
            
            // Get current namespace name
            const char *ns_name = "user";
            if (st && st->current_ns && st->current_ns->name) {
                CljSymbol *ns_sym = as_symbol(st->current_ns->name);
                if (ns_sym) {
                    ns_name = ns_sym->name;
                }
            }
            
            // Create namespaced keyword: :ns/keyword
            int ns_len = strlen(ns_name);
            char *namespaced_keyword = ALLOC(char, ns_len + name_len + 3); // +3 for : and / and \0
            snprintf(namespaced_keyword, ns_len + name_len + 3, ":%s/%s", ns_name, name);
            
            CljObject *result = (CljObject*)intern_symbol_global(namespaced_keyword);
            return result;
        }
        // Check for :ns/keyword (namespaced keyword)
        else if (is_alpha(**input)) {
            // Parse namespace part
            const char *ns_start = *input;
            while (**input && (is_alphanumeric(**input) || **input == '-' || **input == '_')) {
                (*input)++;
            }
            
            if (**input != '/') {
                // Regular keyword :keyword
                *input = start + 1; // Reset to after first ':'
                while (**input && (is_alphanumeric(**input) || **input == '-' || **input == '_')) {
                    (*input)++;
                }
                int len = *input - start;
                char *name = ALLOC(char, len + 1);
                strncpy(name, start, len);
                name[len] = '\0';
                return (CljObject*)intern_symbol_global(name);
            }
            
            (*input)++; // Skip '/'
            
            // Parse keyword name part
            const char *name_start = *input;
            while (**input && (is_alphanumeric(**input) || **input == '-' || **input == '_')) {
                (*input)++;
            }
            
            int ns_len = *input - ns_start - 1; // -1 for '/'
            int name_len = *input - name_start;
            
            if (ns_len == 0 || name_len == 0) {
                printf("Parse error: Invalid namespaced keyword\n");
                return NULL;
            }
            
            // Create namespaced keyword: :ns/keyword
            char *ns_name = ALLOC(char, ns_len + 1);
            strncpy(ns_name, ns_start, ns_len);
            ns_name[ns_len] = '\0';
            
            char *name = ALLOC(char, name_len + 1);
            strncpy(name, name_start, name_len);
            name[name_len] = '\0';
            
            char *namespaced_keyword = ALLOC(char, ns_len + name_len + 3);
            snprintf(namespaced_keyword, ns_len + name_len + 3, ":%s/%s", ns_name, name);
            
            CljObject *result = (CljObject*)intern_symbol_global(namespaced_keyword);
            return result;
        }
        // Regular keyword :keyword
        else {
            while (**input && (is_alphanumeric(**input) || **input == '-' || **input == '_')) {
                (*input)++;
            }
            int len = *input - start;
            char *name = ALLOC(char, len + 1);
            strncpy(name, start, len);
            name[len] = '\0';
            return (CljObject*)intern_symbol_global(name);
        }
    }
    
    // Handle regular symbols (accept UTF-8 codepoints)
    while (**input) {
        unsigned char b = (unsigned char)**input;
        if (is_alphanumeric((char)b)) { (*input)++; continue; }
        if (b & 0x80) { const char *next = utf8codepoint(*input, NULL); if (next == *input) break; *input = next; continue; }
        if (isspace(b) || b=='(' || b==')' || b=='[' || b==']' || b=='{' || b=='}' || b=='"') break;
        break;
    }
    
    int len = *input - start;
    char *name = ALLOC(char, len + 1);
    strncpy(name, start, len);
    name[len] = '\0';
    if (!utf8valid(name)) { printf("Parse error: Invalid UTF-8 in symbol\n"); return NULL; }
    
    return (CljObject*)intern_symbol_global(name);
}

// String parsing with stack buffer
CljObject* parse_string(const char **input, EvalState *st) {
    (void)st;
    char stack_buf[MAX_STACK_STRING_SIZE];
    int pos = 0;
    
    (*input)++; // Skip opening quote
    
    while (**input && **input != '"' && pos < MAX_STACK_STRING_SIZE - 1) {
        if (**input == '\\') {
            (*input)++; // Skip escape character
            if (**input == 'n') stack_buf[pos++] = '\n';
            else if (**input == 't') stack_buf[pos++] = '\t';
            else if (**input == 'r') stack_buf[pos++] = '\r';
            else if (**input == '\\') stack_buf[pos++] = '\\';
            else if (**input == '"') stack_buf[pos++] = '"';
            else stack_buf[pos++] = **input;
        } else {
            stack_buf[pos++] = **input;
        }
        (*input)++;
    }
    
    if (**input != '"') {
        // Parse error occurred
    }
    
    stack_buf[pos] = '\0';
    if (!utf8valid(stack_buf)) { printf("Parse error: Invalid UTF-8 in string\n"); return NULL; }
    (*input)++; // Skip closing quote
    
    return make_string(stack_buf);
}

// Number parsing
CljObject* parse_number(const char **input, EvalState *st) {
    (void)st;
    const char *p = *input;
    int is_float = 0;

    // Optional minus sign
    if (*p == '-') {
        p++;
    }

    // Must start with at least one digit
    if (!is_digit(*p)) {
        return NULL;
    }

    // Integer part
    while (is_digit(*p)) {
        p++;
    }

    // Optional fractional part: '.' followed by at least one digit
    if (*p == '.' && p[1] && is_digit(p[1])) {
        is_float = 1;
        p++; // skip '.'
        while (is_digit(*p)) {
            p++;
        }
    }

    int len = (int)(p - *input);
    char *num_str = ALLOC(char, len + 1);
    strncpy(num_str, *input, (size_t)len);
    num_str[len] = '\0';

    *input = p;

    if (is_float) {
        double val = atof(num_str);
        free(num_str);
        return make_float(val);
    } else {
        int val = atoi(num_str);
        free(num_str);
        return make_int(val);
    }
}

// Meta-Reader-Parser für ^metadata
CljObject* parse_meta_reader(const char **input, EvalState *st) {
    (*input)++; // Skip '^'
    skip_whitespace(input);
    
    // Parse metadata expression
    CljObject *meta = parse_expr(input, st);
    if (!meta) {
        printf("Error: Could not parse metadata expression\n");
        return NULL;
    }
    
    skip_whitespace(input);
    
    // Parse the object that gets the metadata
    CljObject *obj = parse_expr(input, st);
    if (!obj) {
        release(meta);
        // Parse error occurred
        return NULL;
    }
    
    // Apply metadata to object
    meta_set(obj, meta);
    release(meta); // meta_set retains the metadata
    
    return obj;
}

// Meta-Reader-Parser für #^{...} syntax
CljObject* parse_meta_map_reader(const char **input, EvalState *st) {
    (*input)++; // Skip '#'
    (*input)++; // Skip '^'
    
    // Parse metadata map
    CljObject *meta = parse_map(input, st);
    if (!meta) {
        // Parse error occurred
        return NULL;
    }
    
    skip_whitespace(input);
    
    // Parse the object that gets the metadata
    CljObject *obj = parse_expr(input, st);
    if (!obj) {
        release(meta);
        // Parse error occurred
        return NULL;
    }
    
    // Apply metadata to object
    meta_set(obj, meta);
    release(meta); // meta_set retains the metadata
    
    return obj;
}

// with-meta Parser für (with-meta obj meta)
CljObject* parse_with_meta(const char **input, EvalState *st) {
    (*input)++; // Skip '('
    skip_whitespace(input);
    
    // Parse 'with-meta' symbol
    CljObject *fn_name = parse_symbol(input, st);
    if (!fn_name || fn_name->type != CLJ_SYMBOL) {
        if (fn_name) release(fn_name);
        // Parse error occurred
        return NULL;
    }
    
    // Check if it's 'with-meta'
    char *name = (char*)fn_name->as.data;
    if (!name || strcmp(name, "with-meta") != 0) {
        release(fn_name);
        // Parse error occurred
        return NULL;
    }
    release(fn_name);
    
    skip_whitespace(input);
    
    // Parse object
    CljObject *obj = parse_expr(input, st);
    if (!obj) {
        // Parse error occurred
        return NULL;
    }
    
    skip_whitespace(input);
    
    // Parse metadata
    CljObject *meta = parse_expr(input, st);
    if (!meta) {
        release(obj);
        // Parse error occurred
        return NULL;
    }
    
    skip_whitespace(input);
    
    // Expect closing ')'
    if (**input != ')') {
        release(obj);
        release(meta);
        // Parse error occurred
        return NULL;
    }
    (*input)++; // Skip ')'
    
    // Apply metadata to object
    meta_set(obj, meta);
    release(meta); // meta_set retains the metadata
    
    return obj;
}

// Main parsing function
CljObject* parse_expr(const char **input, EvalState *st) {
    skip_whitespace(input);
    skip_comments(input);
    
    if (!**input) {
        return NULL;
    }
    
    char c = **input;
    
    // Check for meta readers first
    if (c == '^') {
        return parse_meta_reader(input, st);
    }
    
    if (c == '#' && (*input)[1] && (*input)[1] == '^') {
        return parse_meta_map_reader(input, st);
    }
    
    switch (c) {
        case '[':
            return parse_vector(input, st);
        case '{':
            return parse_map(input, st);
        case '(':
            // Check if it's with-meta
            {
                const char *save = *input;
                (*input)++; // Skip '('
                skip_whitespace(input);
                
                // Check if next symbol is 'with-meta'
                const char *start = *input;
                while (**input && is_alphanumeric(**input)) {
                    (*input)++;
                }
                int len = *input - start;
                
                if (len == 9 && strncmp(start, "with-meta", 9) == 0) {
                    // Reset and parse as with-meta
                    *input = save;
                    return parse_with_meta(input, st);
                } else {
                    // Reset and parse as normal list
                    *input = save;
                    return parse_list(input, st);
                }
            }
        case '"':
            return parse_string(input, st);
        case ':':
            return parse_symbol(input, st);
        default:
            if ((unsigned char)c & 0x80) {
                return parse_symbol(input, st);
            } else if (is_digit(c) || c == '-') {
                return parse_number(input, st);
            } else if (is_alpha(c)) {
                return parse_symbol(input, st);
            } else if (is_opchar(c)) {
                // Parse single-character operator as symbol
                char name[2];
                name[0] = c;
                name[1] = '\0';
                (*input)++;
                return (CljObject*)intern_symbol_global(name);
            } else {
                // Parse error occurred
                return NULL;
            }
    }
}

// Main parse function
CljObject* parse(const char *input, EvalState *st) {
    const char *input_ptr = input;
    
    // Set up error handling
    if (setjmp(parse_jmp) != 0) {
        // Parse error occurred
        return NULL;
    }
    
    return parse_expr(&input_ptr, st);
}

// Stack-based helpers
CljObject* vector_from_stack(CljObject **stack, int count) {
    CljObject *vec_obj = make_vector(count, 0);
    CljVector *vec = as_vector(vec_obj);
    if (!vec) return NULL;
    for (int i = 0; i < count; i++) {
        vec->data[i] = stack[i];
        retain(stack[i]);
    }
    vec->count = count;
    return vec_obj;
}

CljObject* map_from_stack(CljObject **pairs, int pair_count) {
    if (pair_count == 0) {
        // Empty map literal -> return empty-map singleton
        return make_map(0);
    }
    CljObject *map = make_map(pair_count * 2);
    CljMap *map_data = as_map(map);
    if (!map_data) return NULL;
    
    for (int i = 0; i < pair_count; i++) {
        map_data->data[i * 2] = pairs[i * 2];
        map_data->data[i * 2 + 1] = pairs[i * 2 + 1];
        retain(pairs[i * 2]);
        retain(pairs[i * 2 + 1]);
    }
    map_data->count = pair_count;
    return map;
}

CljObject* list_from_stack(CljObject **stack, int count) {
    if (count == 0) return clj_nil();
    
    // Convert array to linked list
    CljObject *prev = NULL;
    for (int i = count - 1; i >= 0; i--) {
        CljObject *node = make_list();
        CljList *node_list = as_list(node);
        if (!node_list) return clj_nil();
        
        node_list->head = stack[i];
        node_list->tail = prev;
        prev = node;
        retain(stack[i]);
    }
    
    return prev ? prev : clj_nil();
}
