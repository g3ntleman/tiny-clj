#!/usr/bin/env python3
"""
Ctags MCP Tool für Cursor Integration
Nutzt die vorhandene tags-Datei für Code-Navigation und Symbol-Suche
"""

import json
import os
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Any

class CtagsParser:
    """Parser für ctags-Dateien"""
    
    def __init__(self, tags_file_path: str):
        self.tags_file_path = tags_file_path
        self.tags = []
        self._load_tags()
    
    def _load_tags(self):
        """Lädt alle Tags aus der ctags-Datei"""
        if not os.path.exists(self.tags_file_path):
            return
        
        with open(self.tags_file_path, 'r', encoding='utf-8') as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                if line.startswith('!_TAG_'):
                    continue  # Skip metadata lines
                
                tag = self._parse_tag_line(line, line_num)
                if tag:
                    self.tags.append(tag)
    
    def _parse_tag_line(self, line: str, line_num: int) -> Optional[Dict[str, Any]]:
        """Parst eine einzelne Tag-Zeile"""
        # Ctags Format: tagname<TAB>filename<TAB>excmd;"<TAB>extension fields
        parts = line.split('\t')
        if len(parts) < 3:
            return None
        
        tag_name = parts[0]
        filename = parts[1]
        excmd = parts[2]
        
        # Parse extension fields
        ext_fields = {}
        if len(parts) > 3:
            ext_part = parts[3]
            # Remove trailing ;" if present
            if ext_part.endswith(';"'):
                ext_part = ext_part[:-2]
            
            # Parse key:value pairs
            for field in ext_part.split('\t'):
                if ':' in field:
                    key, value = field.split(':', 1)
                    ext_fields[key] = value
        
        return {
            'name': tag_name,
            'filename': filename,
            'excmd': excmd,
            'kind': ext_fields.get('kind', ''),
            'line': int(ext_fields.get('line', 0)) if ext_fields.get('line', '').isdigit() else 0,
            'typeref': ext_fields.get('typeref', ''),
            'scope': ext_fields.get('scope', ''),
            'access': ext_fields.get('access', ''),
            'signature': ext_fields.get('signature', ''),
            'line_number': line_num
        }
    
    def find_symbols(self, query: str, kind: Optional[str] = None, 
                    filename: Optional[str] = None) -> List[Dict[str, Any]]:
        """Sucht Symbole basierend auf Query"""
        results = []
        query_lower = query.lower()
        
        for tag in self.tags:
            # Filter by kind if specified
            if kind and tag['kind'] != kind:
                continue
            
            # Filter by filename if specified
            if filename and filename not in tag['filename']:
                continue
            
            # Check if query matches tag name
            if query_lower in tag['name'].lower():
                results.append(tag)
        
        return sorted(results, key=lambda x: (x['filename'], x['line']))
    
    def find_definition(self, symbol: str, current_file: Optional[str] = None) -> List[Dict[str, Any]]:
        """Findet Definitionen für ein Symbol"""
        # Suche nach exakten Matches zuerst
        exact_matches = [tag for tag in self.tags if tag['name'] == symbol]
        if exact_matches:
            return exact_matches
        
        # Fallback zu partiellen Matches
        return self.find_symbols(symbol)
    
    def find_references(self, symbol: str) -> List[Dict[str, Any]]:
        """Findet alle Referenzen zu einem Symbol"""
        return [tag for tag in self.tags if symbol in tag['name'] or symbol in tag.get('typeref', '')]
    
    def get_symbols_in_file(self, filename: str) -> List[Dict[str, Any]]:
        """Gibt alle Symbole in einer Datei zurück"""
        return [tag for tag in self.tags if filename in tag['filename']]
    
    def get_kinds(self) -> List[str]:
        """Gibt alle verfügbaren Symbol-Arten zurück"""
        kinds = set(tag['kind'] for tag in self.tags if tag['kind'])
        return sorted(list(kinds))

class CtagsMCPTool:
    """MCP Tool für ctags-Integration"""
    
    def __init__(self, workspace_root: str):
        self.workspace_root = Path(workspace_root)
        self.tags_file = self.workspace_root / "tags"
        self.parser = CtagsParser(str(self.tags_file))
    
    def search_symbols(self, query: str, kind: Optional[str] = None, 
                      filename: Optional[str] = None) -> Dict[str, Any]:
        """Sucht Symbole und gibt strukturierte Ergebnisse zurück"""
        results = self.parser.find_symbols(query, kind, filename)
        
        return {
            "query": query,
            "kind_filter": kind,
            "filename_filter": filename,
            "total_results": len(results),
            "symbols": results[:50]  # Limit to 50 results
        }
    
    def find_definition(self, symbol: str, current_file: Optional[str] = None) -> Dict[str, Any]:
        """Findet Definitionen für ein Symbol"""
        definitions = self.parser.find_definition(symbol, current_file)
        
        return {
            "symbol": symbol,
            "current_file": current_file,
            "definitions": definitions[:10]  # Limit to 10 definitions
        }
    
    def find_references(self, symbol: str) -> Dict[str, Any]:
        """Findet alle Referenzen zu einem Symbol"""
        references = self.parser.find_references(symbol)
        
        return {
            "symbol": symbol,
            "references": references[:50]  # Limit to 50 references
        }
    
    def get_file_symbols(self, filename: str) -> Dict[str, Any]:
        """Gibt alle Symbole in einer Datei zurück"""
        symbols = self.parser.get_symbols_in_file(filename)
        
        # Group by kind
        by_kind = {}
        for symbol in symbols:
            kind = symbol['kind'] or 'unknown'
            if kind not in by_kind:
                by_kind[kind] = []
            by_kind[kind].append(symbol)
        
        return {
            "filename": filename,
            "total_symbols": len(symbols),
            "symbols_by_kind": by_kind
        }
    
    def get_available_kinds(self) -> Dict[str, Any]:
        """Gibt verfügbare Symbol-Arten zurück"""
        kinds = self.parser.get_kinds()
        
        # Count symbols per kind
        kind_counts = {}
        for tag in self.parser.tags:
            kind = tag['kind'] or 'unknown'
            kind_counts[kind] = kind_counts.get(kind, 0) + 1
        
        return {
            "available_kinds": kinds,
            "kind_counts": kind_counts,
            "total_tags": len(self.parser.tags)
        }

def main():
    """Hauptfunktion für MCP Tool"""
    if len(sys.argv) < 2:
        print("Usage: python ctags_mcp_tool.py <command> [args...]")
        sys.exit(1)
    
    command = sys.argv[1]
    workspace_root = os.getcwd()
    
    tool = CtagsMCPTool(workspace_root)
    
    try:
        if command == "search":
            query = sys.argv[2] if len(sys.argv) > 2 else ""
            kind = sys.argv[3] if len(sys.argv) > 3 else None
            filename = sys.argv[4] if len(sys.argv) > 4 else None
            
            result = tool.search_symbols(query, kind, filename)
            print(json.dumps(result, indent=2))
        
        elif command == "definition":
            symbol = sys.argv[2] if len(sys.argv) > 2 else ""
            current_file = sys.argv[3] if len(sys.argv) > 3 else None
            
            result = tool.find_definition(symbol, current_file)
            print(json.dumps(result, indent=2))
        
        elif command == "references":
            symbol = sys.argv[2] if len(sys.argv) > 2 else ""
            
            result = tool.find_references(symbol)
            print(json.dumps(result, indent=2))
        
        elif command == "file_symbols":
            filename = sys.argv[2] if len(sys.argv) > 2 else ""
            
            result = tool.get_file_symbols(filename)
            print(json.dumps(result, indent=2))
        
        elif command == "kinds":
            result = tool.get_available_kinds()
            print(json.dumps(result, indent=2))
        
        else:
            print(f"Unknown command: {command}")
            print("Available commands: search, definition, references, file_symbols, kinds")
            sys.exit(1)
    
    except Exception as e:
        print(json.dumps({"error": str(e)}, indent=2))
        sys.exit(1)

if __name__ == "__main__":
    main()

