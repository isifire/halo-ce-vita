"""Inventory the external symbols needed by the complete Vita engine objects.

This is deliberately a link audit rather than a runnable engine link.  It reads
compile_probe.py's --all results, removes symbols supplied by another game
object, records every referring object, and emits JSON plus a short Markdown
report.  The output is suitable for tracking platform-port progress over time.
"""

import argparse
import json
import re
import subprocess
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


CATEGORIES = (
    ("graphics", re.compile(r"(?:^D3D|Direct3D|D3DDevice|D3DX|rasterizer)", re.I)),
    ("audio", re.compile(r"(?:DirectSound|XAudio|sound|audio|wave|mixer)", re.I)),
    ("input", re.compile(r"(?:XInput|DirectInput|input_device|keyboard|mouse)", re.I)),
    ("network", re.compile(r"(?:WSA|socket|send|recv|XNet|network|inet)", re.I)),
    ("filesystem", re.compile(r"(?:CreateFile|ReadFile|WriteFile|CloseHandle|FindFirst|FindNext|SetFile|GetFile|DeleteFile|CreateDirectory|RemoveDirectory)", re.I)),
    ("threads_sync_time", re.compile(r"(?:CreateThread|WaitFor|CriticalSection|Mutex|Semaphore|Event|Interlocked|QueryPerformance|Sleep|Tls|thread)", re.I)),
    ("memory_os", re.compile(r"(?:VirtualAlloc|VirtualFree|VirtualProtect|XPhysical|HeapAlloc|HeapFree)", re.I)),
    ("crt_math", re.compile(r"(?:^_|memcpy|memset|malloc|calloc|realloc|free|printf|sprintf|snprintf|strlen|strcmp|strcpy|strncpy|sin|cos|tan|sqrt|pow|floor|ceil|fmod|rand|qsort)", re.I)),
)


def classify(symbol):
    for name, pattern in CATEGORIES:
        if pattern.search(symbol):
            return name
    return "other"


def inspect_object(nm, record):
    path = Path(record["object"])
    proc = subprocess.run(
        [str(nm), "-g", str(path)], capture_output=True, text=True, check=False
    )
    if proc.returncode:
        return record["source"], set(), set(), proc.stderr.strip()
    defined, undefined = set(), set()
    for line in proc.stdout.splitlines():
        fields = line.split()
        if len(fields) < 2:
            continue
        if fields[-2] in ("U", "w", "v"):
            undefined.add(fields[-1])
        elif len(fields) >= 3 and fields[-2].upper() not in ("U",):
            defined.add(fields[-1])
    return record["source"], defined, undefined, ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()

    records = json.loads(args.results.read_text(encoding="utf-8"))
    if not records or any(not r["compiled"] for r in records):
        raise SystemExit("Incomplete compilation; refusing a partial inventory")
    nm = args.sdk / "bin" / "arm-vita-eabi-nm.exe"
    if not nm.exists():
        nm = args.sdk / "bin" / "arm-vita-eabi-nm"
    if not nm.exists():
        raise SystemExit(f"nm not found below {args.sdk}")

    with ThreadPoolExecutor(max_workers=max(1, min(args.jobs, 16))) as pool:
        inspected = list(pool.map(lambda r: inspect_object(nm, r), records))

    errors = [{"source": source, "error": error} for source, _, _, error in inspected if error]
    definitions = set().union(*(defined for _, defined, _, _ in inspected))
    references = defaultdict(list)
    for source, _, undefined, _ in inspected:
        for symbol in sorted(undefined - definitions):
            references[symbol].append(source)

    symbols = []
    for symbol in sorted(references):
        symbols.append({
            "symbol": symbol,
            "category": classify(symbol),
            "referenced_by": references[symbol],
        })
    counts = Counter(item["category"] for item in symbols)
    report = {
        "schema": 1,
        "compiled_objects": len(records),
        "defined_global_symbols": len(definitions),
        "unresolved_unique_symbols": len(symbols),
        "categories": dict(sorted(counts.items())),
        "inspection_errors": errors,
        "symbols": symbols,
    }

    args.out.mkdir(parents=True, exist_ok=True)
    json_path = args.out / "engine-link-audit.json"
    md_path = args.out / "engine-link-audit.md"
    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# Halo Vita complete-engine link audit",
        "",
        f"Compiled objects inspected: **{len(records)}**",
        f"Unique unresolved symbols: **{len(symbols)}**",
        "",
        "## Categories",
        "",
        "| Category | Symbols |",
        "| --- | ---: |",
    ]
    lines += [f"| {name} | {count} |" for name, count in sorted(counts.items(), key=lambda x: (-x[1], x[0]))]
    lines += ["", "## Symbols", "", "| Category | Symbol | Referenced by |", "| --- | --- | --- |"]
    for item in symbols:
        users = ", ".join(item["referenced_by"][:4])
        if len(item["referenced_by"]) > 4:
            users += f" (+{len(item['referenced_by']) - 4})"
        lines.append(f"| {item['category']} | `{item['symbol']}` | {users} |")
    if errors:
        lines += ["", "## Inspection errors", ""]
        lines += [f"- `{e['source']}`: {e['error']}" for e in errors]
    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"AUDIT objects={len(records)} unresolved={len(symbols)} errors={len(errors)}")
    print(json_path)
    print(md_path)
    if errors:
        raise SystemExit("Object inspection failed; audit is incomplete")


if __name__ == "__main__":
    main()
