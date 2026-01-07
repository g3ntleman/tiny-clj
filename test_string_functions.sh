#!/bin/bash
# Test all 21 clojure.string functions with REPL -e flag

REPL="./build-release/tiny-clj-repl"

echo "=== Testing all 21 clojure.string functions ==="
echo ""

# Test 1: blank?
echo "1. Testing blank?"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/blank? nil)" -e "(clojure.string/blank? \"\")" -e "(clojure.string/blank? \"   \")" -e "(clojure.string/blank? \"abc\")" 2>&1 | tail -5
echo ""

# Test 2: capitalize
echo "2. Testing capitalize"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/capitalize \"hello\")" -e "(clojure.string/capitalize \"HELLO\")" 2>&1 | tail -3
echo ""

# Test 3: ends-with?
echo "3. Testing ends-with?"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/ends-with? \"hello\" \"lo\")" -e "(clojure.string/ends-with? \"hello\" \"x\")" 2>&1 | tail -3
echo ""

# Test 4: includes?
echo "4. Testing includes?"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/includes? \"hello\" \"ell\")" -e "(clojure.string/includes? \"hello\" \"x\")" 2>&1 | tail -3
echo ""

# Test 5: index-of
echo "5. Testing index-of"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/index-of \"hello\" \"l\")" -e "(clojure.string/index-of \"hello\" \"x\")" 2>&1 | tail -3
echo ""

# Test 6: join
echo "6. Testing join"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/join [\"a\" \"b\" \"c\"])" -e "(clojure.string/join \",\" [\"a\" \"b\"])" 2>&1 | tail -3
echo ""

# Test 7: last-index-of
echo "7. Testing last-index-of"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/last-index-of \"hello\" \"l\")" -e "(clojure.string/last-index-of \"hello\" \"x\")" 2>&1 | tail -3
echo ""

# Test 8: lower-case
echo "8. Testing lower-case"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/lower-case \"HELLO\")" -e "(clojure.string/lower-case \"Hello\")" 2>&1 | tail -3
echo ""

# Test 9: replace
echo "9. Testing replace"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/replace \"hello world\" \"world\" \"clojure\")" 2>&1 | tail -2
echo ""

# Test 10: replace-first
echo "10. Testing replace-first"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/replace-first \"hello world world\" \"world\" \"clojure\")" 2>&1 | tail -2
echo ""

# Test 11: reverse
echo "11. Testing reverse"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/reverse \"hello\")" -e "(clojure.string/reverse \"\")" 2>&1 | tail -3
echo ""

# Test 12: split
echo "12. Testing split"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/split \"a,b,c\" \",\")" 2>&1 | tail -2
echo ""

# Test 13: split-lines
echo "13. Testing split-lines"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/split-lines \"a\\nb\\r\\nc\")" 2>&1 | tail -2
echo ""

# Test 14: starts-with?
echo "14. Testing starts-with?"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/starts-with? \"hello\" \"he\")" -e "(clojure.string/starts-with? \"hello\" \"x\")" 2>&1 | tail -3
echo ""

# Test 15: trim
echo "15. Testing trim"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/trim \"  hello  \")" -e "(clojure.string/trim \"\")" 2>&1 | tail -3
echo ""

# Test 16: trim-newline
echo "16. Testing trim-newline"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/trim-newline \"hello\\n\")" -e "(clojure.string/trim-newline \"hello\\r\\n\")" 2>&1 | tail -3
echo ""

# Test 17: triml
echo "17. Testing triml"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/triml \"  hello\")" -e "(clojure.string/triml \"hello  \")" 2>&1 | tail -3
echo ""

# Test 18: trimr
echo "18. Testing trimr"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/trimr \"hello  \")" -e "(clojure.string/trimr \"  hello\")" 2>&1 | tail -3
echo ""

# Test 19: upper-case
echo "19. Testing upper-case"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/upper-case \"hello\")" -e "(clojure.string/upper-case \"Hello\")" 2>&1 | tail -3
echo ""

# Test 20: re-quote-replacement
echo "20. Testing re-quote-replacement"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/re-quote-replacement \"\$100\")" -e "(clojure.string/re-quote-replacement \"normal\")" 2>&1 | tail -3
echo ""

# Test 21: escape
echo "21. Testing escape"
$REPL -e "(require 'clojure.string)" -e "(clojure.string/escape \"a&b\" {\\& \"&amp;\"})" 2>&1 | tail -2
echo ""

echo "=== All tests completed ==="

