# Ctags-MCP Integration für Cursor

Diese Integration ermöglicht es, ctags-basierte Code-Navigation direkt in Cursor zu nutzen, ohne externe Skripte.

## 🚀 Installation

### 1. Dateien sind bereits erstellt:
- `ctags_mcp_tool.py` - Haupt-MCP Tool
- `ctags_cursor_integration.py` - Cursor-spezifische Integration
- `.cursor/mcp_config.json` - Cursor MCP Konfiguration
- `test_ctags_integration.py` - Test-Skript

### 2. Tags-Datei aktualisieren:
```bash
# Generiere neue ctags-Datei
ctags -R --exclude=CMakeFiles --exclude=build-release --exclude=Testing .

# Oder mit erweiterten Optionen
ctags -R --exclude=CMakeFiles --exclude=build-release --exclude=Testing \
      --c-kinds=+p --c++-kinds=+p --fields=+iaS --extras=+q .
```

## 🔧 Verwendung

### Cursor IDE Integration:

#### 1. MCP Server konfigurieren:
Die Datei `.cursor/mcp_config.json` ist bereits konfiguriert:
```json
{
  "mcpServers": {
    "ctags": {
      "command": "python3",
      "args": ["/Users/theisen/Projects/tiny-clj/ctags_mcp_tool.py"],
      "env": {
        "PYTHONPATH": "/Users/theisen/Projects/tiny-clj"
      },
      "description": "Ctags-based symbol search and navigation for tiny-clj project"
    }
  }
}
```

#### 2. In Cursor verwenden:
- **Symbol-Suche**: Über MCP-Interface in Cursor
- **Zur Definition**: Über MCP-Interface in Cursor
- **Alle Referenzen**: Über MCP-Interface in Cursor

### Direkte Kommandozeilen-Nutzung (für Tests):

#### Workspace-Informationen abrufen:
```bash
python3 ctags_cursor_integration.py workspace_info
```

#### Symbol-Suche:
```bash
python3 ctags_cursor_integration.py search "function_name" 20
```

#### Zur Definition springen:
```bash
python3 ctags_cursor_integration.py goto_definition "clj_value_new"
```

## 📊 Verfügbare Funktionen

### Symbol-Suche:
- Fuzzy-Suche nach Symbolnamen
- Filterung nach Symbol-Art (function, variable, etc.)
- Filterung nach Datei
- Begrenzte Ergebnisse für Performance

### Navigation:
- **goto_definition**: Springe zur Definition
- **find_references**: Finde alle Referenzen
- **file_symbols**: Alle Symbole in einer Datei

### Workspace-Info:
- Gesamtanzahl der Symbole
- Verfügbare Symbol-Arten
- Verteilung der Symbole nach Art

## 🧪 Testen

Führe das Test-Skript aus:
```bash
python3 test_ctags_integration.py
```

Das Skript testet:
- ✅ Workspace-Informationen
- ✅ Symbol-Suche
- ✅ Datei-Symbole
- ✅ Definition-Suche
- ✅ MCP Tool Funktionalität

## 📈 Performance

- **1812 Symbole** in der aktuellen tags-Datei
- **Schnelle Suche** durch sortierte Tags
- **Begrenzte Ergebnisse** (50 max) für bessere Performance
- **Caching** der Tags-Datei beim Start

## 🔄 Aktualisierung

### Tags-Datei neu generieren:
```bash
# Einfache Aktualisierung
ctags -R --exclude=CMakeFiles --exclude=build-release --exclude=Testing .

# Mit erweiterten Optionen für bessere Ergebnisse
ctags -R --exclude=CMakeFiles --exclude=build-release --exclude=Testing \
      --c-kinds=+p --c++-kinds=+p --fields=+iaS --extras=+q \
      --regex-C='/^[[:space:]]*#define[[:space:]]+([a-zA-Z_][a-zA-Z0-9_]*)/\1/d,macro/' \
      --regex-C='/^[[:space:]]*typedef[[:space:]]+[^;]+[[:space:]]+([a-zA-Z_][a-zA-Z0-9_]*);/\1/t,typedef/' .
```

### Automatische Aktualisierung:
Erstelle einen Git-Hook für automatische Updates:
```bash
# In .git/hooks/post-commit
#!/bin/bash
ctags -R --exclude=CMakeFiles --exclude=build-release --exclude=Testing .
```

## 🐛 Troubleshooting

### Problem: "No definition found"
- **Lösung**: Tags-Datei aktualisieren
- **Prüfung**: `python3 ctags_cursor_integration.py workspace_info`

### Problem: "Tags file not found"
- **Lösung**: Tags-Datei generieren
- **Befehl**: `ctags -R .`

### Problem: "Permission denied"
- **Lösung**: Ausführungsrechte setzen
- **Befehl**: `chmod +x ctags_*.py`

## 📝 Erweiterte Konfiguration

### Cursor-spezifische Einstellungen:
Bearbeite `ctags_cursor_integration.py` für:
- Maximale Suchergebnisse
- Standard-Symbol-Arten
- Datei-Filter
- Performance-Optimierungen

### MCP Server erweitern:
Bearbeite `ctags_mcp_tool.py` für:
- Zusätzliche Suchfunktionen
- Erweiterte Filter
- Custom Output-Formate
- Integration mit anderen Tools

## 🎯 Nächste Schritte

1. **Cursor MCP Support**: Warte auf vollständige MCP-Integration in Cursor
2. **VS Code Extension**: Erstelle eine VS Code Extension basierend auf diesem Tool
3. **Language Server**: Entwickle einen Language Server Protocol (LSP) Server
4. **Real-time Updates**: Implementiere automatische Tags-Updates bei Datei-Änderungen

## 📚 Weitere Ressourcen

- [Ctags Dokumentation](https://ctags.sourceforge.net/)
- [MCP (Model Context Protocol)](https://modelcontextprotocol.io/)
- [Cursor IDE](https://cursor.sh/)
- [Language Server Protocol](https://microsoft.github.io/language-server-protocol/)
