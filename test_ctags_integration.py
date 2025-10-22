#!/usr/bin/env python3
"""
Test-Skript für ctags Integration
"""

import json
import subprocess
import sys
from pathlib import Path

def test_ctags_tool():
    """Testet das ctags MCP Tool"""
    print("🧪 Teste ctags MCP Tool...")
    
    # Test 1: Workspace Info
    print("\n1. Workspace Info:")
    result = subprocess.run([
        "python3", "ctags_cursor_integration.py", "workspace_info"
    ], capture_output=True, text=True)
    
    if result.returncode == 0:
        info = json.loads(result.stdout)
        print(f"   ✅ Workspace: {info['workspace_root']}")
        print(f"   ✅ Tags-Datei vorhanden: {info['tags_file_exists']}")
        print(f"   ✅ Gesamte Symbole: {info['total_symbols']}")
        print(f"   ✅ Symbol-Arten: {', '.join(info['available_kinds'][:5])}...")
    else:
        print(f"   ❌ Fehler: {result.stderr}")
    
    # Test 2: Symbol-Suche
    print("\n2. Symbol-Suche (nach 'value'):")
    result = subprocess.run([
        "python3", "ctags_cursor_integration.py", "search", "value", "10"
    ], capture_output=True, text=True)
    
    if result.returncode == 0:
        search_result = json.loads(result.stdout)
        print(f"   ✅ Gefunden: {search_result['total']} Symbole")
        for symbol in search_result['symbols'][:3]:
            print(f"      - {symbol['name']} ({symbol['kind']}) in {symbol['file']}:{symbol['line']}")
    else:
        print(f"   ❌ Fehler: {result.stderr}")
    
    # Test 3: Datei-Symbole
    print("\n3. Symbole in src/value.c:")
    result = subprocess.run([
        "python3", "ctags_cursor_integration.py", "file_symbols", "src/value.c"
    ], capture_output=True, text=True)
    
    if result.returncode == 0:
        file_result = json.loads(result.stdout)
        print(f"   ✅ Gefunden: {file_result['total']} Symbole")
        for symbol in file_result['symbols'][:5]:
            print(f"      - {symbol['name']} ({symbol['kind']}) Zeile {symbol['line']}")
    else:
        print(f"   ❌ Fehler: {result.stderr}")
    
    # Test 4: Definition finden
    print("\n4. Definition von 'clj_value_new':")
    result = subprocess.run([
        "python3", "ctags_cursor_integration.py", "goto_definition", "clj_value_new"
    ], capture_output=True, text=True)
    
    if result.returncode == 0:
        def_result = json.loads(result.stdout)
        if def_result['success']:
            print(f"   ✅ Definition gefunden in {def_result['file']}:{def_result['line']}")
            print(f"      Art: {def_result['kind']}")
        else:
            print(f"   ⚠️  {def_result['message']}")
    else:
        print(f"   ❌ Fehler: {result.stderr}")

def test_mcp_tool():
    """Testet das MCP Tool direkt"""
    print("\n🔧 Teste MCP Tool direkt...")
    
    # Test verfügbare Arten
    result = subprocess.run([
        "python3", "ctags_mcp_tool.py", "kinds"
    ], capture_output=True, text=True)
    
    if result.returncode == 0:
        kinds_result = json.loads(result.stdout)
        print(f"   ✅ Verfügbare Symbol-Arten: {len(kinds_result['available_kinds'])}")
        print(f"   ✅ Gesamte Tags: {kinds_result['total_tags']}")
    else:
        print(f"   ❌ Fehler: {result.stderr}")

if __name__ == "__main__":
    print("🚀 Starte ctags Integration Tests...\n")
    
    # Prüfe ob Python-Skripte ausführbar sind
    if not Path("ctags_mcp_tool.py").exists():
        print("❌ ctags_mcp_tool.py nicht gefunden!")
        sys.exit(1)
    
    if not Path("ctags_cursor_integration.py").exists():
        print("❌ ctags_cursor_integration.py nicht gefunden!")
        sys.exit(1)
    
    # Führe Tests aus
    test_ctags_tool()
    test_mcp_tool()
    
    print("\n✅ Tests abgeschlossen!")

