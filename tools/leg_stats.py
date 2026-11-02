"""汇总 [leglimit] 日志：整段的各量极值（限位前→后）、每帧最大转角、限速帧数；查看模式按动作分段。
用法：py -3 leg_stats.py 日志 [日志2 ...]"""
import re
import sys

KEYS = ["髋屈", "髋后摆", "外展", "内收", "髋扭", "膝弯", "反折", "膝侧弯", "膝扭", "踝上", "踝下", "踝偏", "踝翻"]
MINS = {"髋后摆", "内收", "反折", "踝下"}
pat = re.compile(r"(\S+) (-?\d+)→(-?\d+)")
stamp = re.compile(r"^\[(\d{4}\.\d\d\.\d\d-\d\d\.\d\d\.\d\d:\d+)\]")


def summarize(lines):
    agg = {}
    step_raw = step_out = 0.0
    capped = 0
    n = 0
    for ln in lines:
        body = ln.split("[leglimit]", 1)[1]
        for k, a, b in pat.findall(body):
            if k not in KEYS:
                continue
            a, b = int(a), int(b)
            if abs(a) >= 999:
                continue
            r, o = agg.get(k, (None, None))
            f = min if k in MINS else max
            agg[k] = (a if r is None else f(r, a), b if o is None else f(o, b))
        m = re.search(r"每帧最大转角 ([\d.]+)→([\d.]+) 限速帧 (\d+)", body)
        if m:
            step_raw = max(step_raw, float(m.group(1)))
            step_out = max(step_out, float(m.group(2)))
            capped += int(m.group(3))
        n += 1
    s = " ".join(f"{k} {agg[k][0]}→{agg[k][1]}" for k in KEYS if k in agg)
    return f"{s} | 每帧最大转角 {step_raw:.1f}→{step_out:.1f} 限速帧 {capped}（{n} 行）"


for path in sys.argv[1:]:
    lines = open(path, encoding="utf-8", errors="ignore").read().splitlines()
    leg = [l for l in lines if "[leglimit]" in l]
    print("==", path)
    print("  全段", summarize(leg))
    # 查看模式：按 [viewer] 截图行分段（每个动作 2.5 秒，截图在第 2 秒）
    views = [l for l in lines if "[viewer]" in l]
    if views:
        prev = None
        for v in views:
            ts = stamp.match(v).group(1)
            name = v.split("[viewer]", 1)[1].strip().split("   ")[0]
            seg = [l for l in leg if stamp.match(l) and (prev is None or stamp.match(l).group(1) > prev) and stamp.match(l).group(1) <= ts]
            prev = ts
            if seg:
                print("  ", name[:14].ljust(14), summarize(seg))
