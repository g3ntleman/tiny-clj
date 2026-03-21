import sys

with open("subjective-c/src/byte_array.c", "r") as f:
    content = f.read()

# Add the release function and register function
release_funcs = """
static void release_byte_array(CljObject *v) {
    if (!v) return;
    CljByteArray *ba = (CljByteArray *)v;
    if ((v->flags & CLJ_FLAG_EXTERNAL_DATA) == 0 && ba->data) {
        CLJ_FREE(ba->data);
    }
}

void byte_array_register_release_fn(void) {
    subjective_c_register_release_fn(CLJ_BYTE_ARRAY, release_byte_array);
}
"""

if "release_byte_array" not in content:
    content += "\n" + release_funcs

with open("subjective-c/src/byte_array.c", "w") as f:
    f.write(content)
