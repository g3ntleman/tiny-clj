#!/bin/bash
# Test all 21 clojure.string functions with REPL -e flag in a single REPL session

REPL="./build-release/tiny-clj-repl"

echo "=== Testing all 21 clojure.string functions with REPL -e ==="
echo ""

$REPL -e "(require 'clojure.string)" \
  -e "(println \"Testing blank?\")" \
  -e "(clojure.string/blank? nil)" \
  -e "(clojure.string/blank? \"\")" \
  -e "(clojure.string/blank? \"   \")" \
  -e "(clojure.string/blank? \"abc\")" \
  -e "(println \"Testing capitalize\")" \
  -e "(clojure.string/capitalize \"hello\")" \
  -e "(clojure.string/capitalize \"HELLO\")" \
  -e "(println \"Testing ends-with?\")" \
  -e "(clojure.string/ends-with? \"hello\" \"lo\")" \
  -e "(clojure.string/ends-with? \"hello\" \"x\")" \
  -e "(println \"Testing includes?\")" \
  -e "(clojure.string/includes? \"hello\" \"ell\")" \
  -e "(clojure.string/includes? \"hello\" \"x\")" \
  -e "(println \"Testing index-of\")" \
  -e "(clojure.string/index-of \"hello\" \"l\")" \
  -e "(clojure.string/index-of \"hello\" \"x\")" \
  -e "(println \"Testing join\")" \
  -e "(clojure.string/join [\"a\" \"b\" \"c\"])" \
  -e "(clojure.string/join \",\" [\"a\" \"b\"])" \
  -e "(println \"Testing last-index-of\")" \
  -e "(clojure.string/last-index-of \"hello\" \"l\")" \
  -e "(clojure.string/last-index-of \"hello\" \"x\")" \
  -e "(println \"Testing lower-case\")" \
  -e "(clojure.string/lower-case \"HELLO\")" \
  -e "(clojure.string/lower-case \"Hello\")" \
  -e "(println \"Testing replace\")" \
  -e "(clojure.string/replace \"hello world\" \"world\" \"clojure\")" \
  -e "(println \"Testing replace-first\")" \
  -e "(clojure.string/replace-first \"hello world world\" \"world\" \"clojure\")" \
  -e "(println \"Testing reverse\")" \
  -e "(clojure.string/reverse \"hello\")" \
  -e "(clojure.string/reverse \"\")" \
  -e "(println \"Testing split\")" \
  -e "(clojure.string/split \"a,b,c\" \",\")" \
  -e "(println \"Testing split-lines\")" \
  -e "(clojure.string/split-lines \"a\\nb\\r\\nc\")" \
  -e "(println \"Testing starts-with?\")" \
  -e "(clojure.string/starts-with? \"hello\" \"he\")" \
  -e "(clojure.string/starts-with? \"hello\" \"x\")" \
  -e "(println \"Testing trim\")" \
  -e "(clojure.string/trim \"  hello  \")" \
  -e "(clojure.string/trim \"\")" \
  -e "(println \"Testing trim-newline\")" \
  -e "(clojure.string/trim-newline \"hello\\n\")" \
  -e "(clojure.string/trim-newline \"hello\\r\\n\")" \
  -e "(println \"Testing triml\")" \
  -e "(clojure.string/triml \"  hello\")" \
  -e "(clojure.string/triml \"hello  \")" \
  -e "(println \"Testing trimr\")" \
  -e "(clojure.string/trimr \"hello  \")" \
  -e "(clojure.string/trimr \"  hello\")" \
  -e "(println \"Testing upper-case\")" \
  -e "(clojure.string/upper-case \"hello\")" \
  -e "(clojure.string/upper-case \"Hello\")" \
  -e "(println \"Testing re-quote-replacement\")" \
  -e "(clojure.string/re-quote-replacement \"\$100\")" \
  -e "(clojure.string/re-quote-replacement \"normal\")" \
  -e "(println \"All tests completed\")" \
  2>&1 | grep -E "(Testing|RuntimeException|ParseError|nil|true|false)" | head -100

