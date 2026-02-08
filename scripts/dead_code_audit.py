#!/usr/bin/env python3
"""
Dead code auditing helpers for tiny-clj.

This script is intentionally dependency-free (stdlib only) and focuses on
reproducible extraction of build inputs from the top-level CMakeLists.txt.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


REPO_ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class CMakeExecutable:
    name: str
    raw_items: Tuple[str, ...]


@dataclass(frozen=True)
class CMakeInventory:
    cmake_path: str
    sources_var: Tuple[str, ...]
    executables: Tuple[CMakeExecutable, ...]


def _strip_cmake_comment(line: str) -> str:
    # CMake comments start at '#'. There is no string literal escaping we care
    # about here (the top-level file uses it rarely, and our parsing is best-effort).
    return line.split("#", 1)[0]


def _tokenize_items(chunk: str) -> List[str]:
    # Normalize whitespace and split; keep variable references intact.
    chunk = re.sub(r"\s+", " ", chunk.strip())
    if not chunk:
        return []
    return [tok for tok in chunk.split(" ") if tok]


def _scan_cmake_commands(cmake_text: str) -> List[Tuple[str, str]]:
    """
    Return a list of (command_name, command_body) in a best-effort manner.

    command_body is the content inside the outer parentheses, without the
    command name itself.
    """
    commands: List[Tuple[str, str]] = []

    cmd_name: Optional[str] = None
    buf: List[str] = []
    depth = 0

    for raw_line in cmake_text.splitlines():
        line = _strip_cmake_comment(raw_line).strip()
        if not line:
            continue

        if cmd_name is None:
            m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s*\(", line)
            if not m:
                continue
            cmd_name = m.group(1)
            # Start scanning from the first '(' on this line.
            idx = line.find("(")
            rest = line[idx:]
            depth = rest.count("(") - rest.count(")")
            # Remove the leading '('.
            rest_body = rest[1:]
            if rest_body:
                buf.append(rest_body)

            if depth == 0:
                body = " ".join(buf)
                commands.append((cmd_name, body.rsplit(")", 1)[0].strip() if body.endswith(")") else body.strip()))
                cmd_name = None
                buf = []
            continue

        # We are inside a command.
        depth += line.count("(") - line.count(")")
        buf.append(line)
        if depth == 0:
            body = " ".join(buf)
            # Strip the final ')'
            body = body.rsplit(")", 1)[0].strip()
            commands.append((cmd_name, body))
            cmd_name = None
            buf = []

    return commands


def parse_cmake_inventory(cmake_path: Path) -> CMakeInventory:
    text = cmake_path.read_text(encoding="utf-8")
    commands = _scan_cmake_commands(text)

    sources_var: List[str] = []
    executables: List[CMakeExecutable] = []

    for cmd, body in commands:
        cmd_lower = cmd.lower()

        if cmd_lower == "list":
            # Example: list(APPEND SOURCES src/foo.c src/bar.c)
            items = _tokenize_items(body)
            if len(items) >= 3 and items[0].upper() == "APPEND" and items[1] == "SOURCES":
                sources_var.extend(items[2:])
            continue

        if cmd_lower == "add_executable":
            items = _tokenize_items(body)
            if not items:
                continue
            name = items[0]
            raw_items = tuple(items[1:])
            executables.append(CMakeExecutable(name=name, raw_items=raw_items))
            continue

    return CMakeInventory(
        cmake_path=str(cmake_path),
        sources_var=tuple(sources_var),
        executables=tuple(executables),
    )


def _expand_sources(items: Iterable[str], sources_var: Tuple[str, ...]) -> List[str]:
    out: List[str] = []
    for it in items:
        if it == "${SOURCES}":
            out.extend(sources_var)
        else:
            out.append(it)
    return out


def _classify_core_c_sources(paths: Iterable[str]) -> List[str]:
    """
    Define "C-Core" for dead-code audits:
    - src/*.c (excluding src/tests/*)
    - excludes external/ and subjective-c/
    """
    core: List[str] = []
    for p in paths:
        p_norm = p.replace("\\", "/")
        if not p_norm.endswith(".c"):
            continue
        if not p_norm.startswith("src/"):
            continue
        if p_norm.startswith("src/tests/"):
            continue
        core.append(p_norm)
    return sorted(set(core))


def render_inventory_markdown(inv: CMakeInventory) -> str:
    sources_core = _classify_core_c_sources(inv.sources_var)

    lines: List[str] = []
    lines.append("# Dead Code Audit: CMake Inventory")
    lines.append("")
    lines.append(f"Generated from `{Path(inv.cmake_path).name}`.")
    lines.append("")
    lines.append("## SOURCES (C-Core)")
    lines.append("")
    lines.append(f"- **Total entries in `SOURCES`**: {len(inv.sources_var)}")
    lines.append(f"- **C-Core `.c` files in `SOURCES`**: {len(sources_core)}")
    lines.append("")
    for p in sources_core:
        lines.append(f"- `{p}`")

    lines.append("")
    lines.append("## Executables (expanded)")
    lines.append("")
    if not inv.executables:
        lines.append("_No `add_executable(...)` commands detected._")
        lines.append("")
        return "\n".join(lines) + "\n"

    for exe in inv.executables:
        expanded = _expand_sources(exe.raw_items, inv.sources_var)
        core = _classify_core_c_sources(expanded)
        lines.append(f"### `{exe.name}`")
        lines.append("")
        lines.append(f"- **Raw items**: {len(exe.raw_items)}")
        lines.append(f"- **Expanded items**: {len(expanded)}")
        lines.append(f"- **C-Core `.c` files**: {len(core)}")
        lines.append("")
        for p in core:
            lines.append(f"- `{p}`")
        lines.append("")

    return "\n".join(lines) + "\n"


def cmd_inventory(args: argparse.Namespace) -> int:
    cmake_path = Path(args.cmake).resolve()
    inv = parse_cmake_inventory(cmake_path)

    md = render_inventory_markdown(inv)
    if args.out:
        out_path = Path(args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(md, encoding="utf-8")
    else:
        sys.stdout.write(md)

    if args.json_out:
        json_path = Path(args.json_out)
        json_path.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "cmake_path": inv.cmake_path,
            "sources_var": list(inv.sources_var),
            "executables": [{"name": e.name, "raw_items": list(e.raw_items)} for e in inv.executables],
        }
        json_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    return 0


def _run(cmd: List[str], cwd: Path) -> None:
    import subprocess

    proc = subprocess.run(cmd, cwd=str(cwd), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"Command failed ({proc.returncode}): {' '.join(cmd)}\n\n{proc.stdout}")


def _nm_defined_text_symbols(path: Path, include_locals: bool) -> List[str]:
    """
    Extract defined text symbols (T/t) from an object file or final binary.

    Note: For Release builds, linkers can be configured to strip symbols. The
    dead-code audit uses a Release config but overrides link flags to avoid
    stripping symbol tables.
    """
    import subprocess

    cmd = ["nm"]
    if include_locals:
        cmd.append("-a")
    cmd.append(str(path))
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"nm failed for {path}:\n{proc.stderr}")

    syms: List[str] = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        # Common formats:
        # - "0000000100003f50 T _main"
        # - "                 U _printf"
        parts = line.split()
        if len(parts) < 2:
            continue
        # Type is typically the second token, but can be the first if address missing.
        sym_type = None
        sym_name = None
        if len(parts) >= 3 and len(parts[1]) == 1:
            sym_type = parts[1]
            sym_name = parts[2]
        elif len(parts[0]) == 1 and len(parts) >= 2:
            sym_type = parts[0]
            sym_name = parts[1]
        if sym_type not in ("T", "t"):
            continue
        if not sym_name:
            continue
        # Normalize leading '_' (Mach-O) so we can compare across object/binary.
        sym_name = sym_name.lstrip("_")
        syms.append(sym_name)
    return syms


def _classify_object_is_core(obj_path: Path) -> bool:
    p = obj_path.as_posix()
    # In CMake builds we typically see:
    #   .../CMakeFiles/<target>.dir/src/symbol.c.o
    if "/src/" not in p:
        return False
    if "/src/tests/" in p:
        return False
    return True


def cmd_linker(args: argparse.Namespace) -> int:
    build_dir = Path(args.build_dir).resolve()
    out_dir = Path(args.runtime_out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    # Keep symbol tables for nm-based diffs: override Release linker flags.
    # (The repo's Release defaults include -Wl,-S or --strip-all.)
    if sys.platform == "darwin":
        linker_flags = "-Wl,-dead_strip"
    else:
        linker_flags = "-Wl,--gc-sections"

    # Avoid false positives from inlining/LTO:
    # We want "reachable vs unreachable" signals, not "inlined away".
    audit_cflags = (
        "-O2 -DNDEBUG "
        "-ffunction-sections -fdata-sections -fvisibility=hidden "
        "-fno-inline -fno-inline-functions -fno-inline-functions-called-once "
        "-fno-omit-frame-pointer"
    )

    cmake_configure = [
        "cmake",
        "-S",
        str(REPO_ROOT),
        "-B",
        str(build_dir),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY={out_dir}",
        f"-DCMAKE_EXE_LINKER_FLAGS_RELEASE={linker_flags}",
        f"-DCMAKE_C_FLAGS_RELEASE={audit_cflags}",
        "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=OFF",
    ]
    _run(cmake_configure, cwd=REPO_ROOT)

    cmake_build = ["cmake", "--build", str(build_dir), "--target"] + list(args.targets)
    _run(cmake_build, cwd=REPO_ROOT)

    report_dir = Path(args.report_dir).resolve()
    report_dir.mkdir(parents=True, exist_ok=True)
    md_path = report_dir / "dead_code_linker_audit.md"
    json_path = report_dir / "dead_code_linker_audit.json"

    results: Dict[str, Dict[str, object]] = {}

    for target in args.targets:
        binary = out_dir / target
        if not binary.exists():
            raise RuntimeError(f"Built target '{target}' but binary not found in {out_dir}")

        obj_root = build_dir / "CMakeFiles" / f"{target}.dir"
        if not obj_root.exists():
            # Fallback: some generators nest paths; search a bit.
            matches = list(build_dir.rglob(f"CMakeFiles/{target}.dir"))
            obj_root = matches[0] if matches else obj_root
        if not obj_root.exists():
            raise RuntimeError(f"Object directory not found for target '{target}' under {build_dir}")

        object_files = [p for p in obj_root.rglob("*.o") if _classify_object_is_core(p)]

        obj_defined: Dict[str, str] = {}
        for obj in object_files:
            for sym in _nm_defined_text_symbols(obj, include_locals=True):
                # Keep first defining object path.
                obj_defined.setdefault(sym, obj.as_posix())

        bin_defined = set(_nm_defined_text_symbols(binary, include_locals=True))

        stripped = sorted([s for s in obj_defined.keys() if s not in bin_defined])

        # Aggregate by object file for readability.
        by_obj: Dict[str, int] = {}
        for sym in stripped:
            o = obj_defined.get(sym, "?")
            by_obj[o] = by_obj.get(o, 0) + 1
        top_objs = sorted(by_obj.items(), key=lambda kv: kv[1], reverse=True)[:20]

        results[target] = {
            "binary": binary.as_posix(),
            "object_root": obj_root.as_posix(),
            "core_object_files": [p.as_posix() for p in object_files],
            "defined_text_symbols_in_objects": len(obj_defined),
            "defined_text_symbols_in_binary": len(bin_defined),
            "stripped_symbols": [{"name": s, "object": obj_defined.get(s, "?")} for s in stripped],
            "top_objects_by_stripped_count": [{"object": o, "count": c} for (o, c) in top_objs],
        }

    # Write JSON first (canonical data).
    json_path.write_text(json.dumps(results, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    # Render markdown summary.
    lines: List[str] = []
    lines.append("# Dead Code Audit: Linker Stripping Diff (Release)")
    lines.append("")
    lines.append("This report compares symbols defined in core object files against symbols present in the final binaries.")
    lines.append("The **difference** is a practical set of \"linker-stripped\" dead code candidates for the given target(s).")
    lines.append("")
    lines.append(f"- Build dir: `{build_dir}`")
    lines.append(f"- Runtime output dir: `{out_dir}`")
    lines.append(f"- Linker flags override (Release): `{linker_flags}`")
    lines.append("")

    for target, payload in results.items():
        stripped_syms = payload["stripped_symbols"]  # type: ignore[assignment]
        lines.append(f"## `{target}`")
        lines.append("")
        lines.append(f"- Binary: `{payload['binary']}`")
        lines.append(f"- Object root: `{payload['object_root']}`")
        lines.append(f"- Core object files: {len(payload['core_object_files'])}")  # type: ignore[arg-type]
        lines.append(f"- Defined text symbols in objects: {payload['defined_text_symbols_in_objects']}")
        lines.append(f"- Defined text symbols in binary: {payload['defined_text_symbols_in_binary']}")
        lines.append(f"- **Stripped candidates**: {len(stripped_syms)}")  # type: ignore[arg-type]
        lines.append("")

        lines.append("### Top object files by stripped-candidate count")
        lines.append("")
        top = payload["top_objects_by_stripped_count"]  # type: ignore[assignment]
        if not top:
            lines.append("_None._")
        else:
            for row in top:
                lines.append(f"- `{row['object']}`: {row['count']}")
        lines.append("")

        lines.append("### Sample stripped candidates (first 200)")
        lines.append("")
        sample = stripped_syms[:200]  # type: ignore[index]
        if not sample:
            lines.append("_None._")
        else:
            for row in sample:
                lines.append(f"- `{row['name']}` (from `{row['object']}`)")
        lines.append("")

    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


def _extract_warnings_from_compile_commands(db_path: Path, only_src: bool) -> Dict[str, List[str]]:
    """
    Re-run compilation commands with warnings enabled and collect warnings.

    We do not attempt to be a full static analyzer here; this is intended to
    surface things like -Wunused-function and friends consistently across the
    codebase using the existing compile database.
    """
    import subprocess

    db = json.loads(db_path.read_text(encoding="utf-8"))
    warnings_set: Dict[str, set[str]] = {}

    for ent in db:
        file_path = ent.get("file")
        cmd = ent.get("command")
        workdir = ent.get("directory")
        if not file_path or not cmd or not workdir:
            continue

        p = Path(file_path)
        if only_src:
            try:
                rel = p.resolve().relative_to(REPO_ROOT)
            except Exception:
                continue
            rel_posix = rel.as_posix()
            # Strictly limit to the tiny-clj core sources under repo-root/src/
            if not rel_posix.startswith("src/") or rel_posix.startswith("src/tests/"):
                continue

        # Convert to warnings-only: run semantic analysis only.
        #
        # - Use -fsyntax-only so we don't spend time emitting object code.
        # - Drop expensive / irrelevant flags (LTO, optimization level, debug info, arch)
        #   to speed up while keeping warning behavior stable.
        parts = cmd.split(" ")
        # Ensure we don't do linking (compile_commands should already be -c).
        extra = ["-Wextra", "-Wall", "-Wpedantic", "-Wunreachable-code", "-Wunused-function", "-Wunused-variable"]
        # Force output: replace "-o <path>" if present.
        out_parts: List[str] = []
        skip_next = False
        for i, tok in enumerate(parts):
            if skip_next:
                skip_next = False
                continue
            if tok == "-o" and i + 1 < len(parts):
                # With -fsyntax-only, -o is not needed. Drop it entirely.
                skip_next = True
                continue
            # Drop flags that significantly slow down but don't help warnings.
            if tok.startswith("-O"):
                continue
            if tok in ("-g", "-g0", "-g1", "-g2", "-g3"):
                continue
            if tok.startswith("-flto"):
                continue
            if tok == "-arch" and i + 1 < len(parts):
                skip_next = True
                continue
            if tok == "-c":
                # We'll keep compile-stage behavior, but -fsyntax-only supersedes output anyway.
                out_parts.append(tok)
                continue
            out_parts.append(tok)

        out_parts.extend(["-fsyntax-only"])
        out_parts.extend(extra)

        proc = subprocess.run(
            out_parts,
            cwd=workdir,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        out = proc.stdout
        if not out:
            continue

        # Keep only warning lines. Clang warnings contain ": warning:".
        warn_lines = [ln for ln in out.splitlines() if " warning:" in ln]
        if warn_lines:
            s = warnings_set.setdefault(file_path, set())
            for ln in warn_lines:
                s.add(ln)

    # Convert sets to sorted lists for stable output.
    return {k: sorted(v) for (k, v) in warnings_set.items()}


def cmd_warnings(args: argparse.Namespace) -> int:
    db_path = Path(args.compile_commands).resolve()
    if not db_path.exists():
        raise RuntimeError(f"compile_commands.json not found at {db_path}")

    report_dir = Path(args.report_dir).resolve()
    report_dir.mkdir(parents=True, exist_ok=True)
    filt = (args.filter or "all").lower()
    suffix = "" if filt == "all" else f"_{filt}"
    json_path = report_dir / f"dead_code_warnings{suffix}.json"
    md_path = report_dir / f"dead_code_warnings{suffix}.md"

    warnings = _extract_warnings_from_compile_commands(db_path, only_src=args.only_src)

    if filt != "all":
        keep_substrings: List[str]
        if filt == "unused":
            keep_substrings = ["[-Wunused-", "[-Wunreachable-code", "unused function", "unused variable"]
        else:
            raise RuntimeError(f"Unknown --filter value: {args.filter}")
        warnings = {
            fp: [w for w in ws if any(ss in w for ss in keep_substrings)]
            for (fp, ws) in warnings.items()
            if any(any(ss in w for ss in keep_substrings) for w in ws)
        }
    json_path.write_text(json.dumps(warnings, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    # Render markdown
    total_files = len(warnings)
    total_warnings = sum(len(v) for v in warnings.values())
    lines: List[str] = []
    lines.append("# Dead Code Audit: Compiler Warnings Sweep")
    lines.append("")
    lines.append("This report replays compilation commands from `compile_commands.json` and extracts warning lines.")
    lines.append("")
    lines.append(f"- Database: `{db_path}`")
    lines.append(f"- Scope: `{'src-only' if args.only_src else 'all'}`")
    lines.append(f"- Files with warnings: **{total_files}**")
    lines.append(f"- Total warnings captured: **{total_warnings}**")
    lines.append("")

    # Sort by warning count descending
    for file_path, warns in sorted(warnings.items(), key=lambda kv: len(kv[1]), reverse=True):
        lines.append(f"## `{file_path}` ({len(warns)})")
        lines.append("")
        for w in warns[:50]:
            lines.append(f"- {w}")
        if len(warns) > 50:
            lines.append(f"- ... ({len(warns) - 50} more)")
        lines.append("")

    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


def _cov_run(cmd: List[str], cwd: Path, env: Optional[Dict[str, str]] = None) -> None:
    import subprocess

    e = os.environ.copy()
    if env:
        e.update(env)
    proc = subprocess.run(cmd, cwd=str(cwd), env=e, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"Command failed ({proc.returncode}): {' '.join(cmd)}\n\n{proc.stdout}")


def _find_built_binary(runtime_out: Path, target: str) -> Path:
    p = runtime_out / target
    if not p.exists():
        raise RuntimeError(f"Binary for target '{target}' not found in {runtime_out}")
    return p


def _is_core_source_path(path_str: str) -> bool:
    try:
        rel = Path(path_str).resolve().relative_to(REPO_ROOT)
    except Exception:
        return False
    relp = rel.as_posix()
    return relp.startswith("src/") and not relp.startswith("src/tests/") and relp.endswith((".c", ".h"))


def cmd_coverage(args: argparse.Namespace) -> int:
    """
    Build an instrumented binary and report never-executed functions (best-effort).
    """
    build_dir = Path(args.build_dir).resolve()
    runtime_out = Path(args.runtime_out).resolve()
    runtime_out.mkdir(parents=True, exist_ok=True)

    profiles_dir = build_dir / "profiles"
    profiles_dir.mkdir(parents=True, exist_ok=True)
    profraw_pattern = str(profiles_dir / "%p.profraw")
    profdata_path = build_dir / "coverage.profdata"

    # Coverage instrumentation flags (clang/AppleClang).
    cov_cflags = "-O0 -g -fprofile-instr-generate -fcoverage-mapping"
    cov_ldflags = "-fprofile-instr-generate -fcoverage-mapping"

    cmake_configure = [
        "cmake",
        "-S",
        str(REPO_ROOT),
        "-B",
        str(build_dir),
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY={runtime_out}",
        f"-DCMAKE_C_FLAGS_DEBUG={cov_cflags}",
        f"-DCMAKE_EXE_LINKER_FLAGS_DEBUG={cov_ldflags}",
    ]
    _cov_run(cmake_configure, cwd=REPO_ROOT)

    cmake_build = ["cmake", "--build", str(build_dir), "--target"] + list(args.targets)
    _cov_run(cmake_build, cwd=REPO_ROOT)

    # Run unit-tests (primary coverage driver).
    #
    # Some test cases can block indefinitely (e.g. waiting for input). To keep
    # coverage auditing robust, we run the full suite with a timeout and fall
    # back to a curated subset if it times out.
    unit_bin = _find_built_binary(runtime_out, "unit-tests")
    env = {"LLVM_PROFILE_FILE": profraw_pattern}
    import subprocess

    def run_with_timeout(argv: List[str], timeout_s: int) -> Tuple[bool, Optional[int]]:
        """
        Returns (completed, returncode). If timed out, completed=False and returncode=None.

        Coverage is still useful even when some tests fail, so we do not abort
        on non-zero exit codes.
        """
        # start_new_session ensures we can kill the whole process group on timeout.
        p = subprocess.Popen(argv, cwd=str(REPO_ROOT), env={**os.environ, **env}, start_new_session=True)
        try:
            rc = p.wait(timeout=timeout_s)
            return True, rc
        except subprocess.TimeoutExpired:
            try:
                # Kill the whole process group to avoid orphaned test binaries.
                try:
                    os.killpg(p.pid, 9)
                except Exception:
                    p.kill()
            finally:
                try:
                    p.wait(timeout=10)
                except Exception:
                    pass
            return False, None

    failed_runs: List[Dict[str, object]] = []

    ok, rc = run_with_timeout([str(unit_bin)], timeout_s=int(args.test_timeout_seconds))
    if ok and rc not in (0, None):
        failed_runs.append({"argv": [str(unit_bin)], "returncode": rc})

    if not ok:
        # Fallback: run a subset that exercises the interpreter core without
        # known interactive surfaces.
        fallback_patterns = [
            "test_basics*",
            "test_parser*",
            "test_core*",
            "test_eval*",
            "test_symbol*",
            "test_namespace*",
            "test_seq*",
            "test_list*",
            "test_vector*",
            "test_map*",
            "test_string*",
            "test_regex*",
            "test_arithmetic*",
            "test_special_forms*",
        ]
        for pat in fallback_patterns:
            completed, prc = run_with_timeout([str(unit_bin), "--test", pat], timeout_s=int(args.test_timeout_seconds))
            if not completed:
                failed_runs.append(
                    {"argv": [str(unit_bin), "--test", pat], "timeout_seconds": int(args.test_timeout_seconds)}
                )
                continue
            if prc not in (0, None):
                failed_runs.append({"argv": [str(unit_bin), "--test", pat], "returncode": prc})

    # Optional: run a couple of REPL smoke evals to cover REPL-only paths.
    if "tiny-clj-repl" in args.targets:
        repl_bin = _find_built_binary(runtime_out, "tiny-clj-repl")
        _cov_run([str(repl_bin), "-e", "(+ 1 2)"], cwd=REPO_ROOT, env=env)

    # Merge raw profiles.
    profraws = sorted(profiles_dir.glob("*.profraw"))
    if not profraws:
        raise RuntimeError(f"No .profraw files found under {profiles_dir}")
    _cov_run(
        [str(Path("/Library/Developer/CommandLineTools/usr/bin/llvm-profdata")), "merge", "-sparse", "-o", str(profdata_path)]
        + [str(p) for p in profraws],
        cwd=REPO_ROOT,
    )

    # Export coverage as LCOV (includes per-function counts).
    export_path = Path(args.report_dir).resolve()
    export_path.mkdir(parents=True, exist_ok=True)
    lcov_path = export_path / "dead_code_coverage.lcov"
    md_path = export_path / "dead_code_coverage.md"

    llvm_cov = Path("/Library/Developer/CommandLineTools/usr/bin/llvm-cov")
    # Determine core sources to include in coverage export.
    inv = parse_cmake_inventory(REPO_ROOT / "CMakeLists.txt")
    core_sources_rel = _classify_core_c_sources(inv.sources_var)
    if not core_sources_rel:
        raise RuntimeError("No core sources found in CMake SOURCES list")

    export_cmd = [
        str(llvm_cov),
        "export",
        "--format=lcov",
        "--instr-profile",
        str(profdata_path),
        str(unit_bin),
    ]
    for rel in core_sources_rel:
        export_cmd.extend(["--sources", rel])

    import subprocess

    proc = subprocess.run(export_cmd, cwd=str(REPO_ROOT), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"llvm-cov export failed ({proc.returncode})\n\n{proc.stdout}")
    lcov_path.write_text(proc.stdout, encoding="utf-8")

    # Parse LCOV for per-function counts.
    never_called: List[Dict[str, object]] = []
    cur_sf: Optional[str] = None
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("SF:"):
            cur_sf = line[len("SF:") :].strip()
            continue
        if line.startswith("FNDA:"):
            # FNDA:<count>,<name>
            if cur_sf is None:
                continue
            try:
                rest = line[len("FNDA:") :]
                count_str, name = rest.split(",", 1)
                count = int(count_str)
            except Exception:
                continue
            if count != 0:
                continue
            if _is_core_source_path(cur_sf):
                never_called.append({"name": name, "file": cur_sf, "count": count})

    never_called.sort(key=lambda x: (str(x.get("file")), str(x.get("name"))))

    # Summarize.
    lines: List[str] = []
    lines.append("# Dead Code Audit: Coverage (unit-tests)")
    lines.append("")
    lines.append(f"- Build dir: `{build_dir}`")
    lines.append(f"- Runtime output dir: `{runtime_out}`")
    lines.append(f"- Profile data: `{profdata_path}`")
    lines.append(f"- unit-tests binary: `{unit_bin}`")
    lines.append(f"- LCOV export: `{lcov_path}`")
    lines.append("")
    if failed_runs:
        lines.append(f"- Test runs with non-zero exit or timeout: **{len(failed_runs)}** (coverage still generated)")
        lines.append("")
    lines.append(f"- **Never-executed functions (core src)**: **{len(never_called)}**")
    lines.append("")
    lines.append("## Never-executed functions (core src)")
    lines.append("")
    if not never_called:
        lines.append("_None._")
    else:
        for row in never_called[:300]:
            lines.append(f"- `{row['name']}` in `{row['file']}`")
        if len(never_called) > 300:
            lines.append(f"- ... ({len(never_called) - 300} more)")
    lines.append("")
    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    # Also write a compact JSON for the extracted candidates.
    (export_path / "dead_code_coverage_candidates.json").write_text(
        json.dumps({"never_called_core_functions": never_called, "failed_test_runs": failed_runs}, indent=2, sort_keys=True)
        + "\n",
        encoding="utf-8",
    )
    return 0


def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="dead_code_audit.py")
    sub = p.add_subparsers(dest="cmd", required=True)

    inv = sub.add_parser("inventory", help="Extract SOURCES/targets from CMakeLists.txt")
    inv.add_argument(
        "--cmake",
        default=str(REPO_ROOT / "CMakeLists.txt"),
        help="Path to top-level CMakeLists.txt",
    )
    inv.add_argument("--out", default=str(REPO_ROOT / "Reports" / "dead_code_inventory.md"), help="Markdown output path")
    inv.add_argument(
        "--json-out", default=str(REPO_ROOT / "Reports" / "dead_code_inventory.json"), help="JSON output path"
    )
    inv.set_defaults(func=cmd_inventory)

    linker = sub.add_parser("linker", help="Build Release targets and diff stripped symbols via nm")
    linker.add_argument(
        "--build-dir",
        default=str(REPO_ROOT / "build-deadcode-linker"),
        help="CMake build directory (will be created if missing)",
    )
    linker.add_argument(
        "--runtime-out",
        default=str(REPO_ROOT / "build-deadcode-linker" / "out"),
        help="Runtime output directory to avoid clobbering the default build/",
    )
    linker.add_argument(
        "--report-dir",
        default=str(REPO_ROOT / "Reports"),
        help="Where to write dead_code_linker_audit.{md,json}",
    )
    linker.add_argument(
        "targets",
        nargs="*",
        default=["tiny-clj-repl", "unit-tests"],
        help="CMake executable targets to build and audit",
    )
    linker.set_defaults(func=cmd_linker)

    warn = sub.add_parser("warnings", help="Replay compile_commands and extract warning lines")
    warn.add_argument(
        "--compile-commands",
        default=str(REPO_ROOT / "build" / "compile_commands.json"),
        help="Path to compile_commands.json (from an existing build)",
    )
    warn.add_argument(
        "--report-dir",
        default=str(REPO_ROOT / "Reports"),
        help="Where to write dead_code_warnings.{md,json}",
    )
    warn.add_argument("--filter", default="all", help="Filter warnings: all|unused")
    warn.add_argument("--only-src", action="store_true", default=True, help="Limit to src/*.c (excluding src/tests/)")
    warn.set_defaults(func=cmd_warnings)

    cov = sub.add_parser("coverage", help="Build + run unit-tests with llvm-cov and list never-executed functions")
    cov.add_argument(
        "--build-dir",
        default=str(REPO_ROOT / "build-deadcode-coverage"),
        help="CMake build directory for coverage instrumentation",
    )
    cov.add_argument(
        "--runtime-out",
        default=str(REPO_ROOT / "build-deadcode-coverage" / "out"),
        help="Runtime output directory (separate from build/)",
    )
    cov.add_argument("--report-dir", default=str(REPO_ROOT / "Reports"), help="Where to write coverage reports")
    cov.add_argument(
        "--test-timeout-seconds",
        default="180",
        help="Timeout per test run invocation (full suite or single pattern)",
    )
    cov.add_argument(
        "targets",
        nargs="*",
        default=["unit-tests"],
        help="Targets to build (must include unit-tests; optionally tiny-clj-repl)",
    )
    cov.set_defaults(func=cmd_coverage)

    def cmd_candidates(args: argparse.Namespace) -> int:
        report_dir = Path(args.report_dir).resolve()
        linker_json = report_dir / "dead_code_linker_audit.json"
        cov_json = report_dir / "dead_code_coverage_candidates.json"
        if not linker_json.exists():
            raise RuntimeError(f"Missing {linker_json}. Run: dead_code_audit.py linker")
        if not cov_json.exists():
            raise RuntimeError(f"Missing {cov_json}. Run: dead_code_audit.py coverage")

        linker = json.loads(linker_json.read_text(encoding="utf-8"))
        cov = json.loads(cov_json.read_text(encoding="utf-8"))

        # Build sets for quick intersections.
        cov_never = cov.get("never_called_core_functions") or []
        cov_names = set()
        for row in cov_never:
            nm = row.get("name")
            if isinstance(nm, str):
                # LCOV names may contain "file.c:symbol" for static functions; normalize to tail.
                cov_names.add(nm.split(":")[-1])

        # For each target, compute intersection: stripped-by-linker AND never-executed-by-tests.
        out_md = report_dir / "dead_code_candidates.md"
        out_json = report_dir / "dead_code_candidates.json"

        results: Dict[str, object] = {"targets": {}}
        md: List[str] = []
        md.append("# Dead Code Audit: Candidates")
        md.append("")
        md.append("Heuristics:")
        md.append("- **Linker-stripped** (Release `-dead_strip`) symbols are unreachable from the chosen entrypoints.")
        md.append("- **Never-executed** (coverage) functions were not hit by the executed test subset.")
        md.append("- The **intersection** is the highest-confidence dead-code candidate set.")
        md.append("")

        for target, payload in linker.items():
            stripped = payload.get("stripped_symbols") or []
            stripped_names = []
            for row in stripped:
                nm = row.get("name")
                if isinstance(nm, str):
                    stripped_names.append(nm)

            stripped_set = set(stripped_names)
            high = sorted([nm for nm in stripped_set if nm in cov_names])
            results["targets"][target] = {
                "stripped_count": len(stripped_set),
                "coverage_never_called_count": len(cov_names),
                "high_confidence_count": len(high),
                "high_confidence": high,
            }

            md.append(f"## `{target}`")
            md.append("")
            md.append(f"- Linker-stripped candidates: **{len(stripped_set)}**")
            md.append(f"- Coverage never-called (core): **{len(cov_names)}**")
            md.append(f"- **High-confidence (intersection)**: **{len(high)}**")
            md.append("")
            md.append("### High-confidence candidates (first 200)")
            md.append("")
            if not high:
                md.append("_None._")
            else:
                for nm in high[:200]:
                    md.append(f"- `{nm}`")
                if len(high) > 200:
                    md.append(f"- ... ({len(high) - 200} more)")
            md.append("")

        out_json.write_text(json.dumps(results, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        out_md.write_text("\n".join(md) + "\n", encoding="utf-8")
        return 0

    cand = sub.add_parser("candidates", help="Combine linker+coverage outputs into a ranked candidate set")
    cand.add_argument("--report-dir", default=str(REPO_ROOT / "Reports"), help="Directory containing generated reports")
    cand.set_defaults(func=cmd_candidates)

    def cmd_verify(args: argparse.Namespace) -> int:
        """
        Validate high-confidence candidates by checking for:
        - Call-sites in the codebase (via rg)
        - Header declarations (public API)
        - Macro/inline references
        """
        report_dir = Path(args.report_dir).resolve()
        cand_json = report_dir / "dead_code_candidates.json"
        if not cand_json.exists():
            raise RuntimeError(f"Missing {cand_json}. Run: dead_code_audit.py candidates")

        cand = json.loads(cand_json.read_text(encoding="utf-8"))
        target_data = cand.get("targets") or {}
        focus_target = args.target
        if focus_target not in target_data:
            raise RuntimeError(f"Target '{focus_target}' not found in candidates json. Available: {list(target_data.keys())}")

        high_conf = target_data[focus_target].get("high_confidence") or []
        if not high_conf:
            print(f"No high-confidence candidates for {focus_target}.")
            return 0

        import subprocess

        results: List[Dict[str, object]] = []
        for sym in high_conf:
            # Search for call-sites in src/ (excluding tests).
            rg_cmd = ["rg", "-t", "c", "-t", "h", "--count-matches", sym, "src/"]
            proc = subprocess.run(rg_cmd, cwd=str(REPO_ROOT), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
            callsites_found = proc.returncode == 0
            matches = 0
            if callsites_found:
                for line in proc.stdout.splitlines():
                    try:
                        matches += int(line.split(":")[-1])
                    except Exception:
                        pass

            # Check if declared in a header (public API).
            rg_header = ["rg", "-t", "h", "-w", sym, "src/"]
            proc_header = subprocess.run(
                rg_header, cwd=str(REPO_ROOT), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True
            )
            in_header = proc_header.returncode == 0

            # Classify confidence.
            confidence = "HIGH"
            recommendation = "Delete (not called, not in public header)"
            if callsites_found and matches > 0:
                confidence = "FALSE_POSITIVE"
                recommendation = "Keep (has call-sites)"
            elif in_header:
                confidence = "MEDIUM"
                recommendation = "Review (public header, but no call-sites in src/)"

            results.append(
                {
                    "symbol": sym,
                    "confidence": confidence,
                    "callsites_matches": matches,
                    "in_public_header": in_header,
                    "recommendation": recommendation,
                }
            )

        out_json = report_dir / f"dead_code_verified_{focus_target}.json"
        out_md = report_dir / f"dead_code_verified_{focus_target}.md"

        out_json.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")

        # Render markdown.
        md: List[str] = []
        md.append(f"# Dead Code Audit: Verified Candidates (`{focus_target}`)")
        md.append("")
        md.append("Manual verification of high-confidence candidates (linker-stripped + coverage-never-executed).")
        md.append("")
        high = [r for r in results if r["confidence"] == "HIGH"]
        medium = [r for r in results if r["confidence"] == "MEDIUM"]
        false_pos = [r for r in results if r["confidence"] == "FALSE_POSITIVE"]

        md.append(f"- Total high-confidence candidates: {len(results)}")
        md.append(f"- **HIGH confidence (safe to delete)**: **{len(high)}**")
        md.append(f"- MEDIUM confidence (review needed): {len(medium)}")
        md.append(f"- FALSE POSITIVE (has call-sites): {len(false_pos)}")
        md.append("")

        if high:
            md.append("## HIGH Confidence (safe to delete)")
            md.append("")
            for r in high:
                md.append(f"- `{r['symbol']}` → {r['recommendation']}")
            md.append("")

        if medium:
            md.append("## MEDIUM Confidence (review needed)")
            md.append("")
            for r in medium:
                md.append(f"- `{r['symbol']}` → {r['recommendation']}")
            md.append("")

        if false_pos:
            md.append("## FALSE POSITIVE (has call-sites)")
            md.append("")
            for r in false_pos:
                md.append(f"- `{r['symbol']}` ({r['callsites_matches']} matches) → {r['recommendation']}")
            md.append("")

        out_md.write_text("\n".join(md) + "\n", encoding="utf-8")
        return 0

    verify = sub.add_parser("verify", help="Verify high-confidence candidates via rg call-site checks")
    verify.add_argument("--report-dir", default=str(REPO_ROOT / "Reports"), help="Directory containing generated reports")
    verify.add_argument("--target", default="tiny-clj-repl", help="Target to verify (tiny-clj-repl, unit-tests, ...)")
    verify.set_defaults(func=cmd_verify)

    return p


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
