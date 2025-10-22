#!/usr/bin/env python3
"""
Cursor-spezifische ctags Integration
Bietet direkte Funktionen für Cursor IDE Integration
"""

import json
import os
import sys
from pathlib import Path
from ctags_mcp_tool import CtagsMCPTool

class CursorCtagsIntegration:
    """Cursor-spezifische ctags Integration"""
    
    def __init__(self, workspace_root: str = None):
        if workspace_root is None:
            workspace_root = os.getcwd()
        
        self.workspace_root = Path(workspace_root)
        self.tool = CtagsMCPTool(str(self.workspace_root))
    
    def goto_definition(self, symbol: str, current_file: str = None) -> dict:
        """Gehe zur Definition eines Symbols (Cursor-spezifisch)"""
        result = self.tool.find_definition(symbol, current_file)
        
        if result["definitions"]:
            definition = result["definitions"][0]  # Take first match
            return {
                "success": True,
                "file": definition["filename"],
                "line": definition["line"],
                "column": 1,  # ctags doesn't provide column info
                "symbol": symbol,
                "kind": definition["kind"],
                "signature": definition.get("signature", "")
            }
        else:
            return {
                "success": False,
                "message": f"No definition found for symbol: {symbol}"
            }
    
    def find_all_references(self, symbol: str) -> dict:
        """Finde alle Referenzen zu einem Symbol"""
        result = self.tool.find_references(symbol)
        
        references = []
        for ref in result["references"]:
            references.append({
                "file": ref["filename"],
                "line": ref["line"],
                "column": 1,
                "kind": ref["kind"],
                "context": ref.get("excmd", "")
            })
        
        return {
            "success": True,
            "symbol": symbol,
            "references": references,
            "total": len(references)
        }
    
    def get_symbols_in_current_file(self, filename: str) -> dict:
        """Hole alle Symbole in der aktuellen Datei"""
        result = self.tool.get_file_symbols(filename)
        
        symbols = []
        for kind, kind_symbols in result["symbols_by_kind"].items():
            for symbol in kind_symbols:
                symbols.append({
                    "name": symbol["name"],
                    "kind": symbol["kind"],
                    "line": symbol["line"],
                    "signature": symbol.get("signature", ""),
                    "scope": symbol.get("scope", "")
                })
        
        return {
            "success": True,
            "filename": filename,
            "symbols": sorted(symbols, key=lambda x: x["line"]),
            "total": len(symbols)
        }
    
    def search_symbols_fuzzy(self, query: str, limit: int = 20) -> dict:
        """Fuzzy-Suche nach Symbolen"""
        result = self.tool.search_symbols(query)
        
        symbols = []
        for symbol in result["symbols"][:limit]:
            symbols.append({
                "name": symbol["name"],
                "kind": symbol["kind"],
                "file": symbol["filename"],
                "line": symbol["line"],
                "signature": symbol.get("signature", ""),
                "scope": symbol.get("scope", "")
            })
        
        return {
            "success": True,
            "query": query,
            "symbols": symbols,
            "total": result["total_results"]
        }
    
    def get_workspace_info(self) -> dict:
        """Hole Informationen über den Workspace"""
        kinds_result = self.tool.get_available_kinds()
        
        return {
            "success": True,
            "workspace_root": str(self.workspace_root),
            "tags_file_exists": self.tool.tags_file.exists(),
            "total_symbols": kinds_result["total_tags"],
            "available_kinds": kinds_result["available_kinds"],
            "kind_distribution": kinds_result["kind_counts"]
        }

def main():
    """Hauptfunktion für Cursor Integration"""
    if len(sys.argv) < 2:
        print("Usage: python ctags_cursor_integration.py <command> [args...]")
        sys.exit(1)
    
    command = sys.argv[1]
    integration = CursorCtagsIntegration()
    
    try:
        if command == "goto_definition":
            symbol = sys.argv[2] if len(sys.argv) > 2 else ""
            current_file = sys.argv[3] if len(sys.argv) > 3 else None
            
            result = integration.goto_definition(symbol, current_file)
            print(json.dumps(result, indent=2))
        
        elif command == "find_references":
            symbol = sys.argv[2] if len(sys.argv) > 2 else ""
            
            result = integration.find_all_references(symbol)
            print(json.dumps(result, indent=2))
        
        elif command == "file_symbols":
            filename = sys.argv[2] if len(sys.argv) > 2 else ""
            
            result = integration.get_symbols_in_current_file(filename)
            print(json.dumps(result, indent=2))
        
        elif command == "search":
            query = sys.argv[2] if len(sys.argv) > 2 else ""
            limit = int(sys.argv[3]) if len(sys.argv) > 3 else 20
            
            result = integration.search_symbols_fuzzy(query, limit)
            print(json.dumps(result, indent=2))
        
        elif command == "workspace_info":
            result = integration.get_workspace_info()
            print(json.dumps(result, indent=2))
        
        else:
            print(f"Unknown command: {command}")
            print("Available commands: goto_definition, find_references, file_symbols, search, workspace_info")
            sys.exit(1)
    
    except Exception as e:
        print(json.dumps({"success": False, "error": str(e)}, indent=2))
        sys.exit(1)

if __name__ == "__main__":
    main()

