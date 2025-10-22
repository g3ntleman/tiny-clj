#!/usr/bin/env python3
"""
Erweiterte Refactoring-Tool für tiny-clj
Nutzt ctags-mcp für Symbol-Refactoring mit intelligenter Kommentar-Erkennung
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import List, Dict, Any

class AdvancedRefactorTool:
    """Erweiterte Tool für Symbol-Refactoring mit Kommentar-Schutz"""
    
    def __init__(self, workspace_root: str = None):
        if workspace_root is None:
            workspace_root = os.getcwd()
        
        self.workspace_root = Path(workspace_root)
        self.ctags_integration = f"{self.workspace_root}/ctags_cursor_integration.py"
    
    def is_comment_line(self, line: str) -> bool:
        """Prüft ob eine Zeile ein Kommentar ist"""
        stripped = line.strip()
        
        # C/C++ Kommentare
        if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
            return True
        
        # C Block-Kommentare
        if '/*' in stripped and '*/' in stripped:
            return True
        
        # Doxygen Kommentare
        if stripped.startswith('///') or stripped.startswith('* @'):
            return True
        
        return False
    
    def is_in_comment(self, line: str, position: int) -> bool:
        """Prüft ob eine Position in einem Kommentar steht"""
        # Prüfe // Kommentare
        comment_pos = line.find('//')
        if comment_pos != -1 and comment_pos < position:
            return True
        
        # Prüfe /* */ Kommentare
        block_start = line.find('/*')
        if block_start != -1 and block_start < position:
            block_end = line.find('*/', block_start)
            if block_end == -1 or block_end > position:
                return True
        
        return False
    
    def find_all_references(self, symbol: str) -> List[Dict[str, Any]]:
        """Findet alle Referenzen zu einem Symbol"""
        try:
            result = subprocess.run([
                "python3", self.ctags_integration, "find_references", symbol
            ], capture_output=True, text=True, cwd=self.workspace_root)
            
            if result.returncode == 0:
                data = json.loads(result.stdout)
                return data.get("references", [])
            else:
                print(f"Fehler beim Suchen nach Referenzen: {result.stderr}")
                return []
        except Exception as e:
            print(f"Fehler: {e}")
            return []
    
    def find_symbol_occurrences(self, symbol: str) -> List[Dict[str, Any]]:
        """Findet alle Vorkommen eines Symbols im Code (ohne Kommentare)"""
        references = self.find_all_references(symbol)
        occurrences = []
        
        for ref in references:
            file_path = self.workspace_root / ref["file"]
            if file_path.exists():
                try:
                    with open(file_path, 'r', encoding='utf-8') as f:
                        lines = f.readlines()
                    
                    # Suche nach dem Symbol in der Datei
                    for line_num, line in enumerate(lines, 1):
                        # Überspringe Kommentar-Zeilen
                        if self.is_comment_line(line):
                            continue
                        
                        if symbol in line:
                            # Finde alle Vorkommen des Symbols
                            start_pos = 0
                            while True:
                                start_pos = line.find(symbol, start_pos)
                                if start_pos == -1:
                                    break
                                
                                # Prüfe ob das Symbol in einem Kommentar steht
                                if not self.is_in_comment(line, start_pos):
                                    occurrences.append({
                                        "file": ref["file"],
                                        "line": line_num,
                                        "column": start_pos + 1,
                                        "context": line.strip(),
                                        "match": symbol
                                    })
                                
                                start_pos += 1
                
                except Exception as e:
                    print(f"Fehler beim Lesen von {file_path}: {e}")
        
        return occurrences
    
    def preview_rename(self, old_symbol: str, new_symbol: str) -> Dict[str, Any]:
        """Zeigt eine Vorschau der Umbenennung"""
        occurrences = self.find_symbol_occurrences(old_symbol)
        
        preview = {
            "old_symbol": old_symbol,
            "new_symbol": new_symbol,
            "total_occurrences": len(occurrences),
            "files_affected": list(set(occ["file"] for occ in occurrences)),
            "changes": [],
            "skipped_comments": 0
        }
        
        # Zähle auch Kommentare (für Information)
        for ref in self.find_all_references(old_symbol):
            file_path = self.workspace_root / ref["file"]
            if file_path.exists():
                try:
                    with open(file_path, 'r', encoding='utf-8') as f:
                        lines = f.readlines()
                    
                    for line in lines:
                        if old_symbol in line and self.is_comment_line(line):
                            preview["skipped_comments"] += 1
                except:
                    pass
        
        for occ in occurrences:
            old_context = occ["context"]
            new_context = old_context.replace(old_symbol, new_symbol)
            
            preview["changes"].append({
                "file": occ["file"],
                "line": occ["line"],
                "column": occ["column"],
                "old_line": old_context,
                "new_line": new_context
            })
        
        return preview
    
    def rename_symbol(self, old_symbol: str, new_symbol: str, dry_run: bool = True) -> Dict[str, Any]:
        """Benennt ein Symbol um"""
        preview = self.preview_rename(old_symbol, new_symbol)
        
        if dry_run:
            return {
                "status": "preview",
                "message": "Dry-run Modus - keine Dateien wurden geändert",
                "preview": preview
            }
        
        # Führe das tatsächliche Umbenennen durch
        files_changed = set()
        changes_made = 0
        
        for change in preview["changes"]:
            file_path = self.workspace_root / change["file"]
            
            try:
                # Lese die Datei
                with open(file_path, 'r', encoding='utf-8') as f:
                    content = f.read()
                
                # Ersetze das Symbol (nur außerhalb von Kommentaren)
                lines = content.split('\n')
                for i, line in enumerate(lines):
                    if old_symbol in line and not self.is_comment_line(line):
                        # Ersetze nur außerhalb von Kommentaren
                        new_line = line
                        start_pos = 0
                        while True:
                            pos = new_line.find(old_symbol, start_pos)
                            if pos == -1:
                                break
                            
                            if not self.is_in_comment(new_line, pos):
                                new_line = new_line[:pos] + new_symbol + new_line[pos + len(old_symbol):]
                                start_pos = pos + len(new_symbol)
                            else:
                                start_pos = pos + 1
                        
                        lines[i] = new_line
                
                # Schreibe die Datei zurück
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.write('\n'.join(lines))
                
                files_changed.add(change["file"])
                changes_made += 1
                
            except Exception as e:
                print(f"Fehler beim Ändern von {file_path}: {e}")
        
        return {
            "status": "completed",
            "message": f"Symbol umbenannt: {old_symbol} → {new_symbol}",
            "files_changed": list(files_changed),
            "changes_made": changes_made,
            "preview": preview
        }
    
    def get_symbol_info(self, symbol: str) -> Dict[str, Any]:
        """Gibt Informationen über ein Symbol zurück"""
        try:
            # Suche nach Definition
            result = subprocess.run([
                "python3", self.ctags_integration, "goto_definition", symbol
            ], capture_output=True, text=True, cwd=self.workspace_root)
            
            definition = None
            if result.returncode == 0:
                data = json.loads(result.stdout)
                if data.get("success"):
                    definition = data
            
            # Suche nach Referenzen
            references = self.find_all_references(symbol)
            
            return {
                "symbol": symbol,
                "definition": definition,
                "references": references,
                "total_references": len(references)
            }
        
        except Exception as e:
            return {
                "symbol": symbol,
                "error": str(e)
            }

def main():
    """Hauptfunktion für erweiterte Refactoring-Tool"""
    if len(sys.argv) < 2:
        print("Verwendung: python refactor_tool_advanced.py <befehl> [argumente...]")
        print("\nBefehle:")
        print("  info <symbol>              - Zeige Symbol-Informationen")
        print("  preview <alt> <neu>        - Vorschau der Umbenennung")
        print("  rename <alt> <neu>         - Benenne Symbol um (dry-run)")
        print("  rename --execute <alt> <neu> - Benenne Symbol um (tatsächlich)")
        sys.exit(1)
    
    command = sys.argv[1]
    tool = AdvancedRefactorTool()
    
    try:
        if command == "info":
            symbol = sys.argv[2] if len(sys.argv) > 2 else ""
            result = tool.get_symbol_info(symbol)
            print(json.dumps(result, indent=2))
        
        elif command == "preview":
            old_symbol = sys.argv[2] if len(sys.argv) > 2 else ""
            new_symbol = sys.argv[3] if len(sys.argv) > 3 else ""
            result = tool.preview_rename(old_symbol, new_symbol)
            print(json.dumps(result, indent=2))
        
        elif command == "rename":
            if len(sys.argv) > 2 and sys.argv[2] == "--execute":
                old_symbol = sys.argv[3] if len(sys.argv) > 3 else ""
                new_symbol = sys.argv[4] if len(sys.argv) > 4 else ""
                dry_run = False
            else:
                old_symbol = sys.argv[2] if len(sys.argv) > 2 else ""
                new_symbol = sys.argv[3] if len(sys.argv) > 3 else ""
                dry_run = True
            
            result = tool.rename_symbol(old_symbol, new_symbol, dry_run)
            print(json.dumps(result, indent=2))
        
        else:
            print(f"Unbekannter Befehl: {command}")
            sys.exit(1)
    
    except Exception as e:
        print(json.dumps({"error": str(e)}, indent=2))
        sys.exit(1)

if __name__ == "__main__":
    main()

