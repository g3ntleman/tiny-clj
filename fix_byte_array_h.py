with open("subjective-c/src/subjective-c/byte_array.h", "r") as f:
    content = f.read()

decl = "void byte_array_register_release_fn(void);\n"
if decl not in content:
    content = content.replace("#ifdef __cplusplus", decl + "\n#ifdef __cplusplus")
    with open("subjective-c/src/subjective-c/byte_array.h", "w") as f:
        f.write(content)
