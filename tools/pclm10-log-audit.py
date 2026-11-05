#!/usr/bin/env python3
"""Read-only audit of the PCLM10 logs folder.

Hashes every file, parses every run log, and flags provenance anomalies.
It does not build, boot or touch a phone. Output goes to a new directory.
"""
import argparse
import csv
import hashlib
import json
import re
import sys
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path

CST = timezone(timedelta(hours=8))
RUN_RE = re.compile(r"-run\.log$")
LIVE_RE = re.compile(r"-run\.log\.live(?:\.(\d+))?$")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def classify(name: str) -> str:
    if RUN_RE.search(name):
        return "run-log"
    if LIVE_RE.search(name):
        return "live-snapshot"
    if name.endswith("-build.log"):
        return "build-log"
    if "wrapper" in name:
        return "wrapper-log"
    if name.endswith(".png"):
        return "screenshot"
    if name.endswith(".md"):
        return "note"
    if name.endswith(".json"):
        return "json"
    if name.endswith((".bin", ".efi")):
        return "binary"
    if name.endswith((".txt", ".ver")):
        return "receipt"
    return "other"


def parse_run_log(path: Path) -> dict:
    try:
        text = path.read_text(errors="replace")
    except OSError as exc:
        return {"log": path.name, "parse_error": str(exc)}
    lines = text.splitlines()
    entries = {}
    resident = None
    cmdline = None
    cmdline_lines = []
    epoch = None
    kver = None
    return_bytes = None
    return_line = None
    touches = []
    renderers = set()
    no_gpu_draws = 0
    submit_logs = 0
    faults = Counter()
    fault_first = {}
    launchd = []
    springboard = 0
    sb_created = 0
    thermal_max = None
    thermal_reads = None
    kernel_bytes = None
    heartbeat = None
    smmu_refused = 0
    early_end = None

    sb_re = re.compile(r"com\.apple\.springboard|SpringBoard", re.IGNORECASE)
    launchd_re = re.compile(r"com\.apple\.[^|]*\|(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)")
    for number, line in enumerate(lines, 1):
        hit = re.match(r"payload entry '([^']+)' size ([0-9a-fA-F]+)", line)
        if hit:
            entries[hit.group(1)] = int(hit.group(2), 16)
            continue
        hit = re.search(r"resident image at pa ([0-9a-fA-F]+) size ([0-9a-fA-F]+)", line)
        if hit:
            resident = int(hit.group(2), 16)
            continue
        if line.startswith("cmdline: ") and not re.match(r"cmdline: \d+ of \d+", line):
            if cmdline is None:
                cmdline = line[len("cmdline: "):]
            cmdline_lines.append(number)
            m = re.search(r"(?:^| )epoch=(\d+)", line)
            if m:
                epoch = int(m.group(1))
            m = re.search(r"hakios_kver=(\S+)", line)
            if m:
                kver = m.group(1)
            continue
        hit = re.search(r"kernel returned: log at [0-9a-fA-F]+, (\d+) bytes", line)
        if hit:
            return_bytes = int(hit.group(1))
            return_line = number
            continue
        hit = re.search(r"kernel loaded at pa [0-9a-fA-F]+\.\.([0-9a-fA-F]+)", line)
        if hit and kernel_bytes is None:
            pass
        hit = re.search(r"(\d+) downs, (\d+) moves, (\d+) ups, (\d+) reads failed", line)
        if hit:
            touches.append((tuple(int(x) for x in hit.groups()), number))
            continue
        if "context for CoreAnimation" in line:
            m = re.search(r";\s*(FD\d+|Adreno[^;]*)", line)
            if m:
                renderers.add(m.group(1))
            elif "softpipe" in line:
                renderers.add("softpipe")
            elif "llvmpipe" in line:
                renderers.add("llvmpipe")
            continue
        if "no GPU draws" in line:
            no_gpu_draws += 1
            continue
        if "ring WPTR written" in line:
            submit_logs += 1
            continue
        if "THE SMMU REFUSED" in line:
            smmu_refused += 1
        if "SMMU" in line and ("FSR" in line or "fault" in line.lower() or "REFUSED" in line):
            key = re.sub(r"0x[0-9a-fA-F]+", "0xX", line)
            key = re.sub(r"\d+", "N", key).strip()
            faults[key] += 1
            fault_first.setdefault(key, number)
            continue
        for m in launchd_re.finditer(line):
            launchd.append((m.group(1), number))
        if "SOURCE_ATTACH" in line and sb_re.search(line):
            springboard += 1
        if "SpringBoard" in line and "created" in line:
            sb_created += 1
        hit = re.search(r"thermal: (\d+) of (\d+) sensors read; hottest [^ ]+ at ([\d.]+) C", line)
        if hit:
            thermal_reads = (int(hit.group(1)), int(hit.group(2)))
            value = float(hit.group(3))
            thermal_max = max(thermal_max or 0.0, value)
        hit = re.search(r"the kernel has logged (\d+) bytes, heartbeat (\d+)", line)
        if hit:
            kernel_bytes = int(hit.group(1))
            heartbeat = int(hit.group(2))

    if touches:
        best = max(touches)
        touch_max = list(best[0])
        touch_line = best[1]
    else:
        touch_max = touch_line = None

    clock = None
    ideal = None
    anomaly = False
    if epoch and launchd:
        try:
            first = datetime.strptime(launchd[0][0], "%Y-%m-%d %H:%M:%S.%f").replace(tzinfo=CST)
            ideal = datetime.fromtimestamp(epoch, CST)
            clock = first.isoformat()
            anomaly = abs((first - ideal).total_seconds()) > 300
        except ValueError:
            pass
    elif epoch and not launchd:
        anomaly = False

    mtime = datetime.fromtimestamp(path.stat().st_mtime, CST).isoformat()
    return {
        "log": path.name,
        "path": str(path).replace("\\", "/"),
        "size": path.stat().st_size,
        "mtime": mtime,
        "parse_error": None,
        "epoch": epoch,
        "utc": datetime.fromtimestamp(epoch, timezone.utc).isoformat() if epoch else None,
        "local": datetime.fromtimestamp(epoch, CST).isoformat() if epoch else None,
        "kver": kver,
        "device": ("pclm10" if kver and "-sm8150-" in kver else
                   "redwood" if kver and "-sc7280-" in kver else
                   "pclm10" if "thermal_pclm10" in (cmdline or "") else
                   "redwood" if "redwood" in (cmdline or "") else "unknown"),
        "returned": return_bytes is not None,
        "kernel": entries.get("kernel"),
        "ramdisk": entries.get("ramdisk"),
        "cmdline_size": entries.get("cmdline"),
        "payload_entries": entries,
        "resident": resident,
        "cmdline": cmdline,
        "return_bytes": return_bytes,
        "return_line": return_line,
        "touch_max": touch_max,
        "touch_line": touch_line,
        "touch_samples": len(touches),
        "renderers": sorted(renderers),
        "no_gpu_draws": no_gpu_draws,
        "submit_logs": submit_logs,
        "faults": [{"pattern": k, "count": v, "first_line": fault_first[k]} for k, v in faults.most_common(6)],
        "smmu_refused": smmu_refused,
        "launchd_first": launchd[0][0] if launchd else None,
        "launchd_last": launchd[-1][0] if launchd else None,
        "launchd_count": len(launchd),
        "springboard_attach": springboard,
        "springboard_created": sb_created,
        "thermal_max_c": thermal_max,
        "thermal_reads": thermal_reads,
        "clock_anomaly": anomaly,
        "clock_ideal": ideal.isoformat() if ideal else None,
        "last_kernel_bytes": kernel_bytes,
        "heartbeat": heartbeat,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("logs", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    logs = args.logs.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    files = sorted(p for p in logs.rglob("*") if p.is_file() and not p.relative_to(logs).parts[0].startswith("audit-"))
    inventory = []
    hashes = defaultdict(list)
    total = 0
    for index, path in enumerate(files, 1):
        rel = path.relative_to(logs).as_posix()
        size = path.stat().st_size
        total += size
        digest = sha256_file(path)
        hashes[digest].append(rel)
        inventory.append({
            "path": rel,
            "name": path.name,
            "size": size,
            "mtime": datetime.fromtimestamp(path.stat().st_mtime, CST).isoformat(),
            "class": classify(path.name),
            "sha256": digest,
        })
        if index % 500 == 0:
            print(f"hashed {index}/{len(files)}", file=sys.stderr)

    with (out / "inventory.json").open("w", encoding="utf-8") as fh:
        json.dump(inventory, fh, ensure_ascii=False, indent=1)
    with (out / "inventory.csv").open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=["path", "class", "size", "mtime", "sha256"])
        writer.writeheader()
        writer.writerows({k: row[k] for k in writer.fieldnames} for row in inventory)

    runs = []
    for path in sorted(p for p in logs.rglob("*-run.log") if p.is_file()):
        row = parse_run_log(path)
        runs.append(row)
    runs.sort(key=lambda r: (r.get("epoch") is None, r.get("epoch") or 0, r["log"]))

    fields = ["log", "device", "local", "kver", "kernel", "ramdisk", "cmdline_size", "resident",
              "touch_max", "touch_samples", "renderers", "no_gpu_draws", "submit_logs",
              "smmu_refused", "springboard_attach", "springboard_created",
              "thermal_max_c", "return_bytes", "clock_anomaly", "launchd_first", "size", "mtime"]
    with (out / "runs.csv").open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(runs)
    with (out / "runs.json").open("w", encoding="utf-8") as fh:
        json.dump(runs, fh, ensure_ascii=False, indent=1)

    # live families
    live = defaultdict(lambda: {"count": 0, "max_index": -1, "sizes": [], "first_mtime": None, "last_mtime": None})
    for row in inventory:
        m = LIVE_RE.search(row["path"])
        if not m:
            continue
        # "014918-run.log.live.7" -> "014918-run.log"
        base = LIVE_RE.sub("", row["path"]) + "-run.log"
        fam = live[base]
        fam["count"] += 1
        idx = int(m.group(1)) if m.group(1) else 0
        fam["max_index"] = max(fam["max_index"], idx)
        fam["sizes"].append(row["size"])
        if fam["first_mtime"] is None or row["mtime"] < fam["first_mtime"]:
            fam["first_mtime"] = row["mtime"]
        if fam["last_mtime"] is None or row["mtime"] > fam["last_mtime"]:
            fam["last_mtime"] = row["mtime"]
    live_summary = []
    for base, fam in sorted(live.items()):
        final = logs / Path(base).name
        live_summary.append({
            "base": Path(base).name.replace("-run.log", ""),
            "files": fam["count"],
            "max_index": fam["max_index"],
            "has_final_log": final.exists(),
            "first_mtime": fam["first_mtime"],
            "last_mtime": fam["last_mtime"],
        })

    dupes = [{"sha256": d, "count": len(paths), "paths": paths} for d, paths in hashes.items() if len(paths) > 1]
    dupes.sort(key=lambda d: -d["count"])
    classes = Counter(row["class"] for row in inventory)
    parse_errors = [r for r in runs if r.get("parse_error")]
    no_epoch = [r["log"] for r in runs if not r.get("epoch")]
    anomalies = [r for r in runs if r.get("clock_anomaly")]
    touch_runs = [r for r in runs if r.get("touch_max") and max(r["touch_max"][:3]) > 0]
    hw_render = [r for r in runs if r.get("renderers")]
    fault_runs = [r for r in runs if r.get("faults") or r.get("smmu_refused")]
    live_only = [s for s in live_summary if not s["has_final_log"]]
    hangs = [r for r in runs if not r.get("returned")]
    devices = Counter(r.get("device") for r in runs)
    device_runs = {dev: [r for r in runs if r.get("device") == dev] for dev in devices}

    report = {
        "coverage": {
            "files": len(inventory),
            "bytes": total,
            "classes": dict(classes),
            "distinct_sha256": len(hashes),
            "duplicate_groups": len(dupes),
        },
        "runs": {
            "count": len(runs),
            "first_local": runs[0]["local"] if runs else None,
            "last_local": runs[-1]["local"] if runs else None,
            "no_epoch": no_epoch,
            "parse_errors": [r["log"] for r in parse_errors],
            "clock_anomalies": [{"log": r["log"], "kv": r["launchd_first"], "ideal": r["clock_ideal"]} for r in anomalies],
            "touch_runs": [{"log": r["log"], "local": r["local"], "touch": r["touch_max"], "renderers": r["renderers"],
                            "kver": r["kver"], "kernel": r["kernel"], "ramdisk": r["ramdisk"]} for r in touch_runs],
            "renderer_runs": [{"log": r["log"], "local": r["local"], "renderers": r["renderers"],
                               "touch": r["touch_max"], "faults": r["faults"]} for r in hw_render],
            "fault_runs": len(fault_runs),
            "unreturned_runs": [{"log": r["log"], "local": r["local"], "device": r["device"]} for r in hangs],
            "devices": dict(devices),
            "device_span": {dev: {"first": rows[0]["local"], "last": rows[-1]["local"], "count": len(rows)}
                            for dev, rows in device_runs.items() if rows},
        },
        "live": {"families": len(live_summary), "live_only": live_only,
                 "without_final": len(live_only)},
        "duplicates": dupes[:40],
    }
    with (out / "audit.json").open("w", encoding="utf-8") as fh:
        json.dump(report, fh, ensure_ascii=False, indent=1)

    lines = []
    lines.append("# PCLM10 日志全量审计（只读）")
    lines.append("")
    lines.append(f"- 扫描目录：<code>{logs}</code>")
    lines.append(f"- 文件：{len(inventory)} 个，{total/1024/1024:.1f} MiB")
    lines.append(f"- 类别：" + "、".join(f"{k} {v}" for k, v in classes.most_common()))
    lines.append(f"- 不同内容（SHA-256）：{len(hashes)}；重复内容组：{len(dupes)}")
    lines.append(f"- 运行日志：{len(runs)} 份（{runs[0]['local'] if runs else '?'} 至 {runs[-1]['local'] if runs else '?'}）")
    lines.append(f"- 设备分布：" + "、".join(f"{dev} {count}" for dev, count in devices.most_common()))
    for dev, rows in device_runs.items():
        if rows:
            lines.append(f"  - {dev}: {len(rows)} 份，{rows[0]['local']} 至 {rows[-1]['local']}")
    lines.append(f"- 没有返回记录的运行（未见到 kernel returned）：{len(hangs)}")
    lines.append("")
    lines.append("## 运行日志身份与异常")
    lines.append("")
    lines.append(f"- 缺少 epoch 的运行日志：{len(no_epoch)}")
    lines.append(f"- 内部 launchd 墙钟与 epoch 相差超过 5 分钟的：{len(anomalies)}")
    for r in anomalies[:30]:
        lines.append(f"  - {r['log']}: 日志内 {r['launchd_first']}，epoch 应为 {r['clock_ideal']}")
    lines.append(f"- 有触摸事件记录的运行：{len(touch_runs)}")
    for r in touch_runs:
        lines.append(f"  - {r['log']} {r['local']} touch={r['touch_max']} renderer={r['renderers']} kernel={r['kernel']} ramdisk={r['ramdisk']}")
    lines.append(f"- 有 GPU renderer 证据的运行：{len(hw_render)}")
    for r in hw_render[:30]:
        lines.append(f"  - {r['log']} {r['local']} {r['renderers']} touch={r['touch_max']}")
    lines.append("")
    lines.append("## live 快照")
    lines.append(f"- 运行家族：{len(live_summary)}；只有 live 没有最终 run.log：{len(live_only)}")
    for s in live_only[:40]:
        lines.append(f"  - {s['base']}（{s['files']} 段，最后 {s['last_mtime']}）")
    lines.append("")
    lines.append("## 重复内容（前 20 组）")
    for d in dupes[:20]:
        lines.append(f"- {d['count']} 份：{d['sha256'][:16]}… {d['paths'][:4]}")
    (out / "REPORT.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps({"out": str(out), "files": len(inventory), "runs": len(runs),
                      "anomalies": len(anomalies), "touch_runs": len(touch_runs),
                      "live_only": len(live_only)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
