#!/usr/bin/env python3
"""Direct LSP CLI for fast symbol work over stdio LSP servers (clangd, TS, Kotlin, Pyright).

Examples:
  scripts/lsp_cli.py definition src/builtins.c:10:5
  scripts/lsp_cli.py --server typescript definition web/src/app.ts:42:7
  scripts/lsp_cli.py --server kotlin summary app/src/main/kotlin/Foo.kt:12:9
  scripts/lsp_cli.py --server pyright definition tools/foo.py:10:3
  scripts/lsp_cli.py summary src/builtins.c:10:5
  scripts/lsp_cli.py references src/builtins.c:10:5
  scripts/lsp_cli.py callsites src/builtins.c:10:5
  scripts/lsp_cli.py rename src/builtins.c:10:5 new_name --apply
"""

from __future__ import annotations

import argparse
import difflib
import json
import os
import queue
import shlex
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple
from urllib.parse import unquote, urlparse


JSON = Dict[str, Any]
SERVER_PRESETS = ("clangd", "typescript", "vtsls", "kotlin", "pyright")


def eprint(*args: Any) -> None:
    print(*args, file=sys.stderr)


def path_to_uri(path: Path) -> str:
    return path.resolve().as_uri()


def uri_to_path(uri: str) -> Path:
    parsed = urlparse(uri)
    if parsed.scheme != "file":
        raise ValueError(f"Unsupported URI scheme: {uri}")
    return Path(unquote(parsed.path))


def detect_language_id(path: Path) -> str:
    suffix = path.suffix.lower()
    if suffix in {".c", ".h"}:
        return "c"
    if suffix in {".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx"}:
        return "cpp"
    if suffix in {".ts", ".mts", ".cts"}:
        return "typescript"
    if suffix == ".tsx":
        return "typescriptreact"
    if suffix in {".js", ".mjs", ".cjs"}:
        return "javascript"
    if suffix == ".jsx":
        return "javascriptreact"
    if suffix in {".kt", ".kts"}:
        return "kotlin"
    if suffix in {".json"}:
        return "json"
    if suffix in {".py"}:
        return "python"
    return "plaintext"


def parse_location_arg(raw: str, workspace: Path) -> Tuple[Path, int, int]:
    parts = raw.rsplit(":", 2)
    if len(parts) != 3:
        raise ValueError("Expected location format FILE:LINE:COL")
    raw_path, line_s, col_s = parts
    path = Path(raw_path)
    if not path.is_absolute():
        path = (workspace / path).resolve()
    line = int(line_s)
    col = int(col_s)
    if line < 1 or col < 1:
        raise ValueError("LINE and COL must be >= 1")
    return path, line, col


def utf16_units(s: str) -> int:
    return len(s.encode("utf-16-le")) // 2


def utf8_units(s: str) -> int:
    return len(s.encode("utf-8"))


def units_for_text(s: str, encoding: str) -> int:
    if encoding == "utf-8":
        return utf8_units(s)
    # LSP default is utf-16.
    return utf16_units(s)


def lsp_char_from_col1(line_text: str, col1: int, encoding: str) -> int:
    # User columns are 1-based character columns (Unicode code points).
    prefix = line_text[: max(0, col1 - 1)]
    return units_for_text(prefix, encoding)


def col1_from_lsp_char(line_text: str, lsp_char: int, encoding: str) -> int:
    if lsp_char <= 0:
        return 1
    used = 0
    for idx, ch in enumerate(line_text):
        next_used = used + units_for_text(ch, encoding)
        if next_used > lsp_char:
            return idx + 1
        used = next_used
    return len(line_text) + 1


def split_lines_keep_newlines(text: str) -> List[str]:
    lines = text.splitlines(keepends=True)
    if not lines:
        return [""]
    return lines


def strip_trailing_newline(line: str) -> str:
    return line[:-1] if line.endswith("\n") else line


def position_to_abs_offset(text: str, line: int, character: int, encoding: str) -> int:
    if line < 0:
        raise ValueError("Negative line in LSP position")
    lines = split_lines_keep_newlines(text)
    if line >= len(lines):
        raise ValueError(f"Line {line} out of range ({len(lines)} lines)")
    prefix_lines = sum(len(lines[i]) for i in range(line))
    line_text = strip_trailing_newline(lines[line])

    used = 0
    col_idx = 0
    while col_idx < len(line_text) and used < character:
        ch = line_text[col_idx]
        step = units_for_text(ch, encoding)
        if used + step > character:
            break
        used += step
        col_idx += 1
    return prefix_lines + col_idx


@dataclass
class TextEdit:
    start: int
    end: int
    new_text: str


class LspProtocolError(RuntimeError):
    pass


def default_server_bin(server: str) -> str:
    if server == "clangd":
        return "clangd"
    if server == "typescript":
        return "typescript-language-server"
    if server == "vtsls":
        return "vtsls"
    if server == "kotlin":
        return "kotlin-language-server"
    if server == "pyright":
        return "pyright-langserver"
    raise ValueError(f"Unknown server preset: {server}")


def default_server_args(server: str, compile_commands_dir: Optional[Path]) -> List[str]:
    if server == "clangd":
        args: List[str] = []
        if compile_commands_dir is not None:
            args.append(f"--compile-commands-dir={str(compile_commands_dir)}")
        # Quiet down noisy diagnostics; still available on stderr if --verbose.
        args.append("--log=error")
        return args
    if server in {"typescript", "vtsls", "pyright"}:
        return ["--stdio"]
    if server == "kotlin":
        return []
    raise ValueError(f"Unknown server preset: {server}")


def build_server_command_from_preset(
    server: str,
    server_bin_override: Optional[str],
    extra_args: Sequence[str],
    compile_commands_dir: Optional[Path],
) -> List[str]:
    cmd = [server_bin_override or default_server_bin(server)]
    cmd.extend(default_server_args(server, compile_commands_dir))
    cmd.extend(extra_args)
    return cmd


class LspClient:
    def __init__(
        self,
        server_cmd: Sequence[str],
        workspace: Path,
        timeout: float,
        verbose: bool = False,
        server_label: Optional[str] = None,
    ) -> None:
        self.workspace = workspace.resolve()
        self.timeout = timeout
        self.verbose = verbose
        self.server_label = server_label or Path(server_cmd[0]).name
        self.position_encoding = "utf-16"
        self._next_id = 1
        self._queue: "queue.Queue[Optional[JSON]]" = queue.Queue()
        self._stderr_lines: "queue.Queue[str]" = queue.Queue()
        self._open_docs: Dict[str, Tuple[str, int]] = {}

        self.proc = subprocess.Popen(
            list(server_cmd),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert self.proc.stdin is not None
        assert self.proc.stdout is not None
        assert self.proc.stderr is not None

        self._reader = threading.Thread(target=self._read_loop, name="lsp-read", daemon=True)
        self._reader.start()
        self._stderr_reader = threading.Thread(target=self._stderr_loop, name="lsp-stderr", daemon=True)
        self._stderr_reader.start()

        self.initialize()

    def close(self) -> None:
        if self.proc.poll() is not None:
            return
        try:
            self.request("shutdown", {})
        except Exception:
            pass
        try:
            self.notify("exit")
        except Exception:
            pass
        if self.proc.poll() is None:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=1.0)
            except Exception:
                try:
                    self.proc.kill()
                except Exception:
                    pass

    def _stderr_loop(self) -> None:
        assert self.proc.stderr is not None
        for raw in self.proc.stderr:
            line = raw.decode("utf-8", errors="replace").rstrip("\n")
            self._stderr_lines.put(line)
            if self.verbose:
                eprint(f"[{self.server_label}] {line}")

    def _read_loop(self) -> None:
        assert self.proc.stdout is not None
        stream = self.proc.stdout
        try:
            while True:
                headers: Dict[str, str] = {}
                while True:
                    line = stream.readline()
                    if line == b"":
                        self._queue.put(None)
                        return
                    if line in {b"\r\n", b"\n"}:
                        break
                    try:
                        header = line.decode("ascii").strip()
                    except UnicodeDecodeError as exc:
                        raise LspProtocolError(f"Non-ASCII header from clangd: {line!r}") from exc
                    if ":" not in header:
                        raise LspProtocolError(f"Malformed LSP header: {header!r}")
                    key, value = header.split(":", 1)
                    headers[key.strip().lower()] = value.strip()

                if "content-length" not in headers:
                    raise LspProtocolError(f"Missing Content-Length header: {headers!r}")
                length = int(headers["content-length"])
                body = stream.read(length)
                if len(body) != length:
                    self._queue.put(None)
                    return
                msg = json.loads(body.decode("utf-8"))
                self._queue.put(msg)
        except Exception as exc:
            self._queue.put({"_reader_error": str(exc)})

    def _send(self, msg: JSON) -> None:
        assert self.proc.stdin is not None
        data = json.dumps(msg, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        header = f"Content-Length: {len(data)}\r\n\r\n".encode("ascii")
        self.proc.stdin.write(header)
        self.proc.stdin.write(data)
        self.proc.stdin.flush()
        if self.verbose:
            eprint(f">>> {msg['method'] if 'method' in msg else msg}")

    def _await_message(self, timeout: Optional[float] = None) -> JSON:
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Timed out waiting for LSP response")
            try:
                msg = self._queue.get(timeout=remaining)
            except queue.Empty as exc:
                raise TimeoutError("Timed out waiting for LSP response") from exc
            if msg is None:
                raise LspProtocolError("clangd closed the connection")
            if "_reader_error" in msg:
                raise LspProtocolError(str(msg["_reader_error"]))
            return msg

    def request(self, method: str, params: JSON) -> Any:
        req_id = self._next_id
        self._next_id += 1
        self._send({"jsonrpc": "2.0", "id": req_id, "method": method, "params": params})
        while True:
            msg = self._await_message()
            # Ignore notifications and unmatched responses.
            if "id" not in msg:
                continue
            if msg.get("id") != req_id:
                continue
            if "error" in msg:
                raise LspProtocolError(f"{method} failed: {msg['error']}")
            return msg.get("result")

    def notify(self, method: str, params: Optional[JSON] = None) -> None:
        msg: JSON = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params
        self._send(msg)

    def initialize(self) -> None:
        caps: JSON = {
            "workspace": {
                "applyEdit": True,
                "workspaceEdit": {"documentChanges": True},
            },
            "textDocument": {
                "definition": {},
                "references": {},
                "hover": {},
                "rename": {"prepareSupport": True},
                "callHierarchy": {},
            },
            "general": {"positionEncodings": ["utf-8", "utf-16"]},
            # clangd extension (older versions); other servers should ignore it.
            "offsetEncoding": ["utf-8", "utf-16"],
        }
        params: JSON = {
            "processId": os.getpid(),
            "rootUri": path_to_uri(self.workspace),
            "clientInfo": {"name": "tiny-clj-lsp-cli", "version": "0.1"},
            "capabilities": caps,
            "workspaceFolders": [
                {
                    "uri": path_to_uri(self.workspace),
                    "name": self.workspace.name,
                }
            ],
        }
        result = self.request("initialize", params)
        server_caps = (result or {}).get("capabilities", {})
        pos_enc = server_caps.get("positionEncoding")
        if isinstance(pos_enc, str):
            self.position_encoding = pos_enc
        self.notify("initialized", {})

    def open_doc(self, path: Path) -> Tuple[str, int]:
        path = path.resolve()
        uri = path_to_uri(path)
        if uri in self._open_docs:
            return self._open_docs[uri]

        text = path.read_text(encoding="utf-8")
        version = 1
        self._open_docs[uri] = (text, version)
        self.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": detect_language_id(path),
                    "version": version,
                    "text": text,
                }
            },
        )
        return text, version

    def definition(self, path: Path, line1: int, col1: int) -> Any:
        text, _ = self.open_doc(path)
        lines = split_lines_keep_newlines(text)
        if line1 - 1 >= len(lines):
            raise ValueError(f"Line {line1} out of range for {path}")
        line_text = strip_trailing_newline(lines[line1 - 1])
        char = lsp_char_from_col1(line_text, col1, self.position_encoding)
        return self.request(
            "textDocument/definition",
            {
                "textDocument": {"uri": path_to_uri(path)},
                "position": {"line": line1 - 1, "character": char},
            },
        )

    def prepare_rename(self, path: Path, line1: int, col1: int) -> Any:
        text, _ = self.open_doc(path)
        lines = split_lines_keep_newlines(text)
        if line1 - 1 >= len(lines):
            raise ValueError(f"Line {line1} out of range for {path}")
        line_text = strip_trailing_newline(lines[line1 - 1])
        char = lsp_char_from_col1(line_text, col1, self.position_encoding)
        return self.request(
            "textDocument/prepareRename",
            {
                "textDocument": {"uri": path_to_uri(path)},
                "position": {"line": line1 - 1, "character": char},
            },
        )

    def rename(self, path: Path, line1: int, col1: int, new_name: str) -> Any:
        text, _ = self.open_doc(path)
        lines = split_lines_keep_newlines(text)
        if line1 - 1 >= len(lines):
            raise ValueError(f"Line {line1} out of range for {path}")
        line_text = strip_trailing_newline(lines[line1 - 1])
        char = lsp_char_from_col1(line_text, col1, self.position_encoding)
        return self.request(
            "textDocument/rename",
            {
                "textDocument": {"uri": path_to_uri(path)},
                "position": {"line": line1 - 1, "character": char},
                "newName": new_name,
            },
        )

    def references(self, path: Path, line1: int, col1: int, include_declaration: bool = False) -> Any:
        text, _ = self.open_doc(path)
        lines = split_lines_keep_newlines(text)
        if line1 - 1 >= len(lines):
            raise ValueError(f"Line {line1} out of range for {path}")
        line_text = strip_trailing_newline(lines[line1 - 1])
        char = lsp_char_from_col1(line_text, col1, self.position_encoding)
        return self.request(
            "textDocument/references",
            {
                "textDocument": {"uri": path_to_uri(path)},
                "position": {"line": line1 - 1, "character": char},
                "context": {"includeDeclaration": include_declaration},
            },
        )

    def summary(self, path: Path, line1: int, col1: int) -> Any:
        """User-facing summary command backed by LSP hover."""
        text, _ = self.open_doc(path)
        lines = split_lines_keep_newlines(text)
        if line1 - 1 >= len(lines):
            raise ValueError(f"Line {line1} out of range for {path}")
        line_text = strip_trailing_newline(lines[line1 - 1])
        char = lsp_char_from_col1(line_text, col1, self.position_encoding)
        return self.request(
            "textDocument/hover",
            {
                "textDocument": {"uri": path_to_uri(path)},
                "position": {"line": line1 - 1, "character": char},
            },
        )

    def prepare_call_hierarchy(self, path: Path, line1: int, col1: int) -> Any:
        text, _ = self.open_doc(path)
        lines = split_lines_keep_newlines(text)
        if line1 - 1 >= len(lines):
            raise ValueError(f"Line {line1} out of range for {path}")
        line_text = strip_trailing_newline(lines[line1 - 1])
        char = lsp_char_from_col1(line_text, col1, self.position_encoding)
        return self.request(
            "textDocument/prepareCallHierarchy",
            {
                "textDocument": {"uri": path_to_uri(path)},
                "position": {"line": line1 - 1, "character": char},
            },
        )

    def incoming_calls(self, item: JSON) -> Any:
        return self.request("callHierarchy/incomingCalls", {"item": item})

    def outgoing_calls(self, item: JSON) -> Any:
        return self.request("callHierarchy/outgoingCalls", {"item": item})


def normalize_definition_result(result: Any) -> List[Tuple[Path, int, int]]:
    if result is None:
        return []
    items = result if isinstance(result, list) else [result]
    out: List[Tuple[Path, int, int]] = []
    for item in items:
        if "uri" in item and "range" in item:
            uri = item["uri"]
            pos = item["range"]["start"]
        elif "targetUri" in item and "targetSelectionRange" in item:
            uri = item["targetUri"]
            pos = item["targetSelectionRange"]["start"]
        elif "targetUri" in item and "targetRange" in item:
            uri = item["targetUri"]
            pos = item["targetRange"]["start"]
        else:
            continue
        out.append((uri_to_path(uri), int(pos["line"]), int(pos["character"])))
    return out


def normalize_location_result(result: Any) -> List[Tuple[Path, int, int]]:
    if result is None:
        return []
    items = result if isinstance(result, list) else [result]
    out: List[Tuple[Path, int, int]] = []
    for item in items:
        if not isinstance(item, dict):
            continue
        if "uri" not in item or "range" not in item:
            continue
        pos = item["range"]["start"]
        out.append((uri_to_path(item["uri"]), int(pos["line"]), int(pos["character"])))
    return out


def normalize_call_hierarchy_items(result: Any) -> List[JSON]:
    if result is None:
        return []
    if isinstance(result, list):
        return [item for item in result if isinstance(item, dict)]
    if isinstance(result, dict):
        return [result]
    return []


def normalize_incoming_calls_result(result: Any) -> List[Tuple[Path, int, int, str]]:
    if result is None:
        return []
    if not isinstance(result, list):
        return []
    out: List[Tuple[Path, int, int, str]] = []
    for call in result:
        if not isinstance(call, dict):
            continue
        src = call.get("from")
        ranges = call.get("fromRanges") or []
        if not isinstance(src, dict) or "uri" not in src:
            continue
        caller_name = str(src.get("name", "<unknown>"))
        path = uri_to_path(src["uri"])
        for rng in ranges:
            try:
                start = rng["start"]
                out.append((path, int(start["line"]), int(start["character"]), caller_name))
            except Exception:
                continue
    return out


def normalize_outgoing_calls_result(origin_item: JSON, result: Any) -> List[Tuple[Path, int, int, str, Path]]:
    if result is None:
        return []
    if not isinstance(result, list):
        return []
    origin_uri = origin_item.get("uri")
    if not isinstance(origin_uri, str):
        return []
    origin_path = uri_to_path(origin_uri)
    out: List[Tuple[Path, int, int, str, Path]] = []
    for call in result:
        if not isinstance(call, dict):
            continue
        target = call.get("to")
        ranges = call.get("fromRanges") or []
        if not isinstance(target, dict):
            continue
        callee_name = str(target.get("name", "<unknown>"))
        callee_path = uri_to_path(target["uri"]) if "uri" in target else origin_path
        for rng in ranges:
            try:
                start = rng["start"]
                out.append((origin_path, int(start["line"]), int(start["character"]), callee_name, callee_path))
            except Exception:
                continue
    return out


def _render_hover_part(part: Any) -> str:
    if part is None:
        return ""
    if isinstance(part, str):
        return part
    if isinstance(part, dict):
        if "kind" in part and "value" in part:
            return str(part["value"])
        if "language" in part and "value" in part:
            value = str(part["value"])
            lang = str(part["language"]).strip()
            if not lang:
                return value
            return f"[{lang}]\n{value}"
    return json.dumps(part, ensure_ascii=False)


def format_summary_result(result: Any) -> str:
    if not result:
        return ""
    contents = result.get("contents") if isinstance(result, dict) else result
    if isinstance(contents, list):
        parts = [_render_hover_part(part).strip() for part in contents]
        return "\n\n".join(part for part in parts if part)
    return _render_hover_part(contents).strip()


def lsp_pos_to_line_col1(path: Path, line0: int, char: int, encoding: str) -> Tuple[int, int]:
    text = path.read_text(encoding="utf-8")
    lines = split_lines_keep_newlines(text)
    if line0 >= len(lines):
        return line0 + 1, 1
    line_text = strip_trailing_newline(lines[line0])
    col1 = col1_from_lsp_char(line_text, char, encoding)
    return line0 + 1, col1


def workspace_edit_to_file_edits(workspace_edit: JSON) -> Dict[Path, List[Dict[str, Any]]]:
    file_edits: Dict[Path, List[Dict[str, Any]]] = {}
    # Prefer documentChanges when present; it carries richer metadata.
    if workspace_edit.get("documentChanges"):
        pass
    elif "changes" in workspace_edit and workspace_edit["changes"]:
        for uri, edits in workspace_edit["changes"].items():
            path = uri_to_path(uri)
            file_edits.setdefault(path, []).extend(edits)
    for change in workspace_edit.get("documentChanges", []) or []:
        kind = change.get("kind")
        if kind in {"create", "rename", "delete"}:
            raise LspProtocolError(f"Resource operations not supported yet: {change}")
        text_document = change.get("textDocument")
        edits = change.get("edits")
        if not text_document or edits is None:
            raise LspProtocolError(f"Unsupported documentChanges entry: {change}")
        path = uri_to_path(text_document["uri"])
        # Skip annotation wrappers; clangd typically returns plain text edits.
        normalized: List[Dict[str, Any]] = []
        for edit in edits:
            if "newText" in edit and "range" in edit:
                normalized.append(edit)
            elif "textEdit" in edit and isinstance(edit["textEdit"], dict):
                normalized.append(edit["textEdit"])
            else:
                raise LspProtocolError(f"Unsupported edit shape: {edit}")
        file_edits.setdefault(path, []).extend(normalized)
    return file_edits


def apply_text_edits_to_text(text: str, edits: Iterable[Dict[str, Any]], encoding: str) -> str:
    converted: List[TextEdit] = []
    for edit in edits:
        rng = edit["range"]
        start = rng["start"]
        end = rng["end"]
        start_off = position_to_abs_offset(text, int(start["line"]), int(start["character"]), encoding)
        end_off = position_to_abs_offset(text, int(end["line"]), int(end["character"]), encoding)
        converted.append(TextEdit(start=start_off, end=end_off, new_text=edit["newText"]))

    converted.sort(key=lambda e: (e.start, e.end), reverse=True)
    result = text
    last_start = None
    for edit in converted:
        if edit.start > edit.end:
            raise LspProtocolError(f"Invalid edit range: {edit}")
        if last_start is not None and edit.end > last_start:
            raise LspProtocolError("Overlapping edits in workspace edit")
        result = result[: edit.start] + edit.new_text + result[edit.end :]
        last_start = edit.start
    return result


def render_unified_diff(path: Path, old: str, new: str) -> str:
    diff = difflib.unified_diff(
        old.splitlines(keepends=True),
        new.splitlines(keepends=True),
        fromfile=str(path),
        tofile=str(path),
    )
    return "".join(diff)


def find_default_workspace(start: Path) -> Path:
    here = start.resolve()
    for candidate in [here, *here.parents]:
        if (candidate / ".git").exists():
            return candidate
    return here


def find_default_compile_commands_dir(workspace: Path) -> Optional[Path]:
    candidates = [workspace / "build", workspace]
    for cand in candidates:
        if (cand / "compile_commands.json").exists():
            return cand
    return None


def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Direct stdio-LSP CLI for definitions, references, call sites, summaries, and renames."
    )
    p.add_argument(
        "--server",
        choices=SERVER_PRESETS,
        default="clangd",
        help="LSP server preset (default: clangd)",
    )
    p.add_argument(
        "--server-bin",
        default=None,
        help="Override executable for the selected --server preset",
    )
    p.add_argument(
        "--server-command",
        default=None,
        help="Full LSP server command as a shell-style string (overrides --server preset)",
    )
    p.add_argument(
        "--server-arg",
        action="append",
        default=[],
        help="Extra argument appended to the server command (repeatable)",
    )
    # Backward-compat alias for existing local usage. Equivalent to --server clangd --server-bin <value>.
    p.add_argument("--clangd", default=None, help=argparse.SUPPRESS)
    p.add_argument("--workspace", default=None, help="Workspace root (default: auto-detect via .git)")
    p.add_argument(
        "--compile-commands-dir",
        default=None,
        help="Directory containing compile_commands.json (clangd preset only; default: build/ if present)",
    )
    p.add_argument("--timeout", type=float, default=10.0, help="Per-request timeout in seconds")
    p.add_argument("--verbose", action="store_true", help="Print server stderr and sent requests")

    sub = p.add_subparsers(dest="command", required=True)

    p_def = sub.add_parser("definition", help="Find definition(s) for symbol at FILE:LINE:COL")
    p_def.add_argument("location", help="FILE:LINE:COL (1-based)")
    p_def.add_argument("--json", action="store_true", help="Print raw-ish JSON output")

    p_sum = sub.add_parser("summary", help="Show LSP symbol summary for FILE:LINE:COL (backed by hover)")
    p_sum.add_argument("location", help="FILE:LINE:COL (1-based)")
    p_sum.add_argument("--json", action="store_true", help="Print raw hover JSON output")

    p_refs = sub.add_parser("references", help="Find references for symbol at FILE:LINE:COL")
    p_refs.add_argument("location", help="FILE:LINE:COL (1-based)")
    p_refs.add_argument(
        "--include-declaration",
        action="store_true",
        help="Include declaration/definition locations in the reference results",
    )
    p_refs.add_argument("--json", action="store_true", help="Print raw references JSON output")

    p_calls = sub.add_parser(
        "callsites",
        help="List incoming call sites for function/symbol at FILE:LINE:COL (call hierarchy, fallback to references)",
    )
    p_calls.add_argument("location", help="FILE:LINE:COL (1-based)")
    p_calls.add_argument("--json", action="store_true", help="Print raw incoming-calls JSON (or fallback references)")

    p_in = sub.add_parser("incoming-calls", help="List incoming call sites via LSP Call Hierarchy")
    p_in.add_argument("location", help="FILE:LINE:COL (1-based)")
    p_in.add_argument("--json", action="store_true", help="Print raw incoming-calls JSON output")

    p_out = sub.add_parser("outgoing-calls", help="List outgoing calls from function via LSP Call Hierarchy")
    p_out.add_argument("location", help="FILE:LINE:COL (1-based)")
    p_out.add_argument("--json", action="store_true", help="Print raw outgoing-calls JSON output")

    p_ren = sub.add_parser("rename", help="Rename symbol at FILE:LINE:COL")
    p_ren.add_argument("location", help="FILE:LINE:COL (1-based)")
    p_ren.add_argument("new_name", help="New symbol name")
    p_ren.add_argument("--apply", action="store_true", help="Write changes to files (default: preview diff only)")
    p_ren.add_argument("--skip-prepare", action="store_true", help="Skip textDocument/prepareRename")
    p_ren.add_argument("--json", action="store_true", help="Print raw workspace edit JSON")

    return p


def cmd_definition(args: argparse.Namespace, client: LspClient, workspace: Path) -> int:
    path, line1, col1 = parse_location_arg(args.location, workspace)
    result = client.definition(path, line1, col1)
    if args.json:
        print(json.dumps(result, indent=2))
        return 0

    locations = normalize_definition_result(result)
    if not locations:
        eprint("No definition found")
        return 2

    seen: set[Tuple[str, int, int]] = set()
    for def_path, line0, char in locations:
        line_out, col_out = lsp_pos_to_line_col1(def_path, line0, char, client.position_encoding)
        key = (str(def_path), line_out, col_out)
        if key in seen:
            continue
        seen.add(key)
        print(f"{def_path}:{line_out}:{col_out}")
    return 0


def _print_locations(
    locations: List[Tuple[Path, int, int]],
    client: LspClient,
    *,
    empty_message: str,
) -> int:
    if not locations:
        eprint(empty_message)
        return 2

    seen: set[Tuple[str, int, int]] = set()
    count = 0
    for path, line0, char in locations:
        line_out, col_out = lsp_pos_to_line_col1(path, line0, char, client.position_encoding)
        key = (str(path), line_out, col_out)
        if key in seen:
            continue
        seen.add(key)
        count += 1
        print(f"{path}:{line_out}:{col_out}")
    return 0 if count else 2


def cmd_summary(args: argparse.Namespace, client: LspClient, workspace: Path) -> int:
    path, line1, col1 = parse_location_arg(args.location, workspace)
    result = client.summary(path, line1, col1)
    if args.json:
        print(json.dumps(result, indent=2))
        return 0
    text = format_summary_result(result)
    if not text:
        eprint("No summary available")
        return 2
    print(text)
    return 0


def cmd_references(args: argparse.Namespace, client: LspClient, workspace: Path) -> int:
    path, line1, col1 = parse_location_arg(args.location, workspace)
    result = client.references(path, line1, col1, include_declaration=args.include_declaration)
    if args.json:
        print(json.dumps(result, indent=2))
        return 0
    locations = normalize_location_result(result)
    return _print_locations(locations, client, empty_message="No references found")


def _call_hierarchy_prepare_first(
    client: LspClient, path: Path, line1: int, col1: int
) -> Tuple[Optional[JSON], Optional[str]]:
    try:
        prepared = client.prepare_call_hierarchy(path, line1, col1)
    except LspProtocolError as exc:
        return None, str(exc)
    items = normalize_call_hierarchy_items(prepared)
    if not items:
        return None, None
    return items[0], None


def cmd_callsites(args: argparse.Namespace, client: LspClient, workspace: Path) -> int:
    path, line1, col1 = parse_location_arg(args.location, workspace)
    item, prepare_err = _call_hierarchy_prepare_first(client, path, line1, col1)
    if item is not None:
        try:
            result = client.incoming_calls(item)
        except LspProtocolError as exc:
            prepare_err = str(exc)
            result = None
        if args.json:
            if result is not None:
                print(json.dumps(result, indent=2))
                return 0
        if result is not None:
            rows = normalize_incoming_calls_result(result)
            if rows:
                seen: set[Tuple[str, int, int, str]] = set()
                count = 0
                for row_path, line0, char, caller_name in rows:
                    line_out, col_out = lsp_pos_to_line_col1(row_path, line0, char, client.position_encoding)
                    key = (str(row_path), line_out, col_out, caller_name)
                    if key in seen:
                        continue
                    seen.add(key)
                    count += 1
                    print(f"{row_path}:{line_out}:{col_out}  [caller: {caller_name}]")
                if count:
                    return 0
                eprint("No call sites found")
                return 2

    # Fallback for symbols / servers without Call Hierarchy support.
    if prepare_err:
        eprint(f"Call hierarchy unavailable, falling back to references: {prepare_err}")
    result = client.references(path, line1, col1, include_declaration=False)
    if args.json:
        print(json.dumps(result, indent=2))
        return 0
    locations = normalize_location_result(result)
    return _print_locations(locations, client, empty_message="No call sites found (fallback references)")


def cmd_incoming_calls(args: argparse.Namespace, client: LspClient, workspace: Path) -> int:
    path, line1, col1 = parse_location_arg(args.location, workspace)
    item, prepare_err = _call_hierarchy_prepare_first(client, path, line1, col1)
    if item is None:
        if prepare_err:
            eprint(f"Call hierarchy unavailable: {prepare_err}")
        else:
            eprint("No call hierarchy item found at location")
        return 2

    try:
        result = client.incoming_calls(item)
    except LspProtocolError as exc:
        eprint(f"Call hierarchy unavailable: {exc}")
        return 2
    if args.json:
        print(json.dumps(result, indent=2))
        return 0
    rows = normalize_incoming_calls_result(result)
    if not rows:
        eprint("No incoming calls found")
        return 2

    seen: set[Tuple[str, int, int, str]] = set()
    count = 0
    for row_path, line0, char, caller_name in rows:
        line_out, col_out = lsp_pos_to_line_col1(row_path, line0, char, client.position_encoding)
        key = (str(row_path), line_out, col_out, caller_name)
        if key in seen:
            continue
        seen.add(key)
        count += 1
        print(f"{row_path}:{line_out}:{col_out}  [caller: {caller_name}]")
    return 0 if count else 2


def cmd_outgoing_calls(args: argparse.Namespace, client: LspClient, workspace: Path) -> int:
    path, line1, col1 = parse_location_arg(args.location, workspace)
    item, prepare_err = _call_hierarchy_prepare_first(client, path, line1, col1)
    if item is None:
        if prepare_err:
            eprint(f"Call hierarchy unavailable: {prepare_err}")
        else:
            eprint("No call hierarchy item found at location")
        return 2

    try:
        result = client.outgoing_calls(item)
    except LspProtocolError as exc:
        eprint(f"Call hierarchy unavailable: {exc}")
        return 2
    if args.json:
        print(json.dumps(result, indent=2))
        return 0
    rows = normalize_outgoing_calls_result(item, result)
    if not rows:
        eprint("No outgoing calls found")
        return 2

    seen: set[Tuple[str, int, int, str, str]] = set()
    count = 0
    for row_path, line0, char, callee_name, callee_path in rows:
        line_out, col_out = lsp_pos_to_line_col1(row_path, line0, char, client.position_encoding)
        key = (str(row_path), line_out, col_out, callee_name, str(callee_path))
        if key in seen:
            continue
        seen.add(key)
        count += 1
        print(f"{row_path}:{line_out}:{col_out}  [callee: {callee_name}] ({callee_path})")
    return 0 if count else 2


def cmd_rename(args: argparse.Namespace, client: LspClient, workspace: Path) -> int:
    path, line1, col1 = parse_location_arg(args.location, workspace)

    if not args.skip_prepare:
        try:
            prep = client.prepare_rename(path, line1, col1)
            if prep is None:
                eprint("prepareRename returned null; symbol may not be renameable here")
                return 2
        except LspProtocolError as exc:
            eprint(f"prepareRename failed: {exc}")
            return 2

    edit = client.rename(path, line1, col1, args.new_name)
    if args.json:
        print(json.dumps(edit, indent=2))
        return 0
    if not edit:
        eprint("Rename returned no edits")
        return 2

    file_edits = workspace_edit_to_file_edits(edit)
    if not file_edits:
        eprint("Rename returned an empty workspace edit")
        return 2

    changed_files = 0
    total_edits = 0
    for file_path in sorted(file_edits):
        old = file_path.read_text(encoding="utf-8")
        new = apply_text_edits_to_text(old, file_edits[file_path], client.position_encoding)
        if old == new:
            continue
        changed_files += 1
        total_edits += len(file_edits[file_path])
        if args.apply:
            file_path.write_text(new, encoding="utf-8")
        else:
            diff = render_unified_diff(file_path, old, new)
            if diff:
                print(diff, end="" if diff.endswith("\n") else "\n")

    if changed_files == 0:
        eprint("No file content changes produced")
        return 2

    mode = "applied" if args.apply else "previewed"
    eprint(f"{mode} rename edits across {changed_files} file(s), {total_edits} edit(s)")
    if not args.apply:
        eprint("Re-run with --apply to write the changes")
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)

    cwd = Path.cwd()
    workspace = Path(args.workspace).resolve() if args.workspace else find_default_workspace(cwd)

    if args.clangd and args.server_bin:
        parser.error("Use either --server-bin or --clangd, not both")

    server_preset = args.server
    server_bin = args.server_bin or args.clangd

    compile_commands_dir: Optional[Path] = None
    if server_preset == "clangd" and not args.server_command:
        if args.compile_commands_dir:
            compile_commands_dir = Path(args.compile_commands_dir)
            if not compile_commands_dir.is_absolute():
                compile_commands_dir = (workspace / compile_commands_dir).resolve()
        else:
            compile_commands_dir = find_default_compile_commands_dir(workspace)

        if compile_commands_dir is not None and not (compile_commands_dir / "compile_commands.json").exists():
            parser.error(f"compile_commands.json not found in {compile_commands_dir}")

    if args.server_command:
        server_cmd = shlex.split(args.server_command)
        if not server_cmd:
            parser.error("--server-command produced an empty command")
        server_cmd.extend(args.server_arg)
        server_label = Path(server_cmd[0]).name
    else:
        server_cmd = build_server_command_from_preset(
            server=server_preset,
            server_bin_override=server_bin,
            extra_args=args.server_arg,
            compile_commands_dir=compile_commands_dir,
        )
        server_label = server_preset

    client = LspClient(
        server_cmd=server_cmd,
        workspace=workspace,
        timeout=args.timeout,
        verbose=args.verbose,
        server_label=server_label,
    )
    try:
        if args.command == "definition":
            return cmd_definition(args, client, workspace)
        if args.command == "summary":
            return cmd_summary(args, client, workspace)
        if args.command == "references":
            return cmd_references(args, client, workspace)
        if args.command == "callsites":
            return cmd_callsites(args, client, workspace)
        if args.command == "incoming-calls":
            return cmd_incoming_calls(args, client, workspace)
        if args.command == "outgoing-calls":
            return cmd_outgoing_calls(args, client, workspace)
        if args.command == "rename":
            return cmd_rename(args, client, workspace)
        parser.error(f"Unknown command: {args.command}")
        return 2
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
