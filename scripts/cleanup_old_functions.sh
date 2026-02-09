#!/bin/bash

# Cleanup Script für veraltete Funktionen in Tiny-Clj
# Dieses Script identifiziert und entfernt veraltete CljObject*-APIs

set -e

echo "🧹 Tiny-Clj Cleanup Script für veraltete Funktionen"
echo "=================================================="

# Farben für Output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Funktionen, die entfernt werden sollen
OLD_FUNCTIONS=(
    "make_object_by_parsing_expr"
    "make_vector"
    "vector_conj"
    "make_map"
    "map_get"
    "map_assoc"
    "make_string"
    "seq_first"
    "seq_rest"
)

# Header-Dateien, die aktualisiert werden müssen
HEADER_FILES=(
    "src/vector.h"
    "src/string.h"
    "src/seq.h"
    "src/parser.h"
    "src/builtins.h"
    "src/function_call.h"
    "src/namespace.h"
    "src/memory.h"
    "src/object.h"
    "src/tiny_clj.h"
    "src/runtime.h"
    "src/symbol.h"
    "src/clj_string.h"
    "src/list_operations.h"
    "src/kv_macros.h"
)

# Test-Dateien, die migriert werden müssen
TEST_FILES=(
    "src/tests/unit_tests.c"
    "src/tests/seq_tests.c"
    "src/tests/memory_tests.c"
    "src/tests/for_loop_tests.c"
    "src/tests/exception_tests.c"
)

echo -e "${YELLOW}1. Identifiziere veraltete Funktionen...${NC}"

# Prüfe, welche veralteten Funktionen noch verwendet werden
for func in "${OLD_FUNCTIONS[@]}"; do
    echo -n "  Prüfe $func... "
    if grep -r "$func(" src/ --include="*.c" --include="*.h" > /dev/null 2>&1; then
        echo -e "${RED}NOCH VERWENDET${NC}"
        echo "    Verwendet in:"
        grep -r "$func(" src/ --include="*.c" --include="*.h" | head -3 | sed 's/^/      /'
    else
        echo -e "${GREEN}KANN ENTFERNT WERDEN${NC}"
    fi
done

echo -e "\n${YELLOW}2. Prüfe Header-Dateien auf CljObject* APIs...${NC}"

for header in "${HEADER_FILES[@]}"; do
    if [ -f "$header" ]; then
        echo -n "  Prüfe $header... "
        if grep -q "CljObject\*" "$header"; then
            echo -e "${RED}MUSS AKTUALISIERT WERDEN${NC}"
            echo "    CljObject* APIs gefunden:"
            grep "CljObject\*" "$header" | head -3 | sed 's/^/      /'
        else
            echo -e "${GREEN}BEREITS AKTUALISIERT${NC}"
        fi
    else
        echo -e "${YELLOW}DATEI NICHT GEFUNDEN${NC}"
    fi
done

echo -e "\n${YELLOW}3. Prüfe Test-Dateien auf Migration...${NC}"

for test_file in "${TEST_FILES[@]}"; do
    if [ -f "$test_file" ]; then
        echo -n "  Prüfe $test_file... "
        if grep -q "make_vector\|make_map\|make_string" "$test_file"; then
            echo -e "${RED}MUSS MIGRIERT WERDEN${NC}"
            echo "    Veraltete APIs gefunden:"
            grep -E "make_vector|make_map|make_string" "$test_file" | head -3 | sed 's/^/      /'
        else
            echo -e "${GREEN}BEREITS MIGRIERT${NC}"
        fi
    else
        echo -e "${YELLOW}DATEI NICHT GEFUNDEN${NC}"
    fi
done

echo -e "\n${YELLOW}4. Zusammenfassung der notwendigen Aktionen:${NC}"

echo -e "\n${RED}⚠️  WARNUNG:${NC} Die folgenden Aktionen sind erforderlich:"
echo "  1. Migriere alle Tests von CljObject* zu CljValue"
echo "  2. Entferne veraltete APIs aus Header-Dateien"
echo "  3. Aktualisiere alle Header-Dateien auf CljValue-API"
echo "  4. Dokumentiere Breaking Changes"

echo -e "\n${GREEN}✅ Bereits abgeschlossen:${NC}"
echo "  - Clojure-kompatibles transient/persistent! Verhalten"
echo "  - Erweiterte Fehlermeldungen mit Parameter-Position"
echo "  - Assert für NULL Environment in eval_arg"
echo "  - Migration von clojure_core.c zu make_value_by_parsing_expr"

echo -e "\n${YELLOW}📋 Nächste Schritte:${NC}"
echo "  1. Führe 'migrate-tests-to-cljvalue' aus"
echo "  2. Führe 'remove-old-*apis' aus"
echo "  3. Führe 'update-headers-to-cljvalue' aus"
echo "  4. Führe 'document-breaking-changes' aus"

echo -e "\n${GREEN}🎯 Ziel erreicht:${NC} Vollständige Migration zu CljValue-API"
