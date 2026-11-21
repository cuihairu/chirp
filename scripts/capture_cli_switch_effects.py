#!/usr/bin/env python3
"""配置开关效果证据采集（服务端 CLI 旗标）。

覆盖口径：能不带凭据、不依赖外部网络就证明「开≠关」的旗标。当前清单：

  --metrics_port <port>  默认 0=不监听；给端口后 libs/common 的 MetricsHttpServer
                         起来。off = 该端口连不上（connection refused），on =
                         GET /health 返回 200 "OK" 且 GET /metrics 200。
                         实测发现：/metrics 的正文是空的——仓内没有任何服务调
                         SetMetricsHandler（只有 tests/unit/common_metrics_test.cc
                         调过），而 counter/gauge 是在 SimpleMetrics 里收集的
                         （services/shared/chat/src/enhanced_dispatch.cc 有
                         CHIRP_GAUGE_INC("chirp_chat_sessions")），收了没导出。
                         脚本把正文长度如实打印出来，空就空、非空就打印样本行。

未覆盖（同一页的「尚未覆盖」段有原因）：--push_transport / --apns-* 这组要一个
把 chirp_app_notification 接进栈的 rig（桌面 dev 栈根本没起这个服务，gateway 侧
还有 --notification_host/--notification_port/--notification_secret 要配），且真实
投递证明又卡在 APNs/FCM 凭据上（todo L25）。

取证形态说明：本机没有 xterm/asciinema，录不了真终端录屏。这里做的是**把上面两次
真实运行的实测输出渲染成终端样式的帧**，再用 ffmpeg 缝成 GIF——帧内每一行都是实测
拿到的文本（连 curl 的报错串都是真的），不是手写文案。

用法：python3 scripts/capture_cli_switch_effects.py
产物落 docs/screenshots/switches/cli-metrics-port-{off,on}.png 与 .gif。
"""

import http.client
import os
import shutil
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parents[1]
CHIRP_CHAT = REPO / "build/services/shared/chat/chirp_chat"
OUT = REPO / "docs/screenshots/switches"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
BG, PROMPT, TEXT, ERROR = "#1e1e28", "#7ec699", "#e6e6e6", "#e57373"
W, H = 900, 430
NAME = "cli-metrics-port"


def pick_port(used):
    """bind(0) 取一个空闲端口；记录到 used 里避免同一轮重复挑到同一个。"""
    for _ in range(20):
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
        s.close()
        if port not in used:
            used.add(port)
            return port
    raise RuntimeError("no free port")


def start_chat(extra_flags):
    proc = subprocess.Popen(
        [str(CHIRP_CHAT), "--port", str(extra_flags[0]), "--ws_port", str(extra_flags[1]), *extra_flags[2:]],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    return proc


def stop(proc):
    if proc.poll() is None:
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            proc.wait(timeout=5)


def wait_port(port, deadline=25.0):
    end = time.time() + deadline
    while time.time() < end:
        s = socket.socket()
        s.settimeout(0.5)
        try:
            if s.connect_ex(("127.0.0.1", port)) == 0:
                s.close()
                return True
        finally:
            s.close()
        time.sleep(0.25)
    return False


def port_dead(port):
    """--metrics_port 0 的判据：该端口不接受连接。"""
    s = socket.socket()
    s.settimeout(1.0)
    try:
        return s.connect_ex(("127.0.0.1", port)) != 0
    finally:
        s.close()


def get_path(metrics_port, path):
    conn = http.client.HTTPConnection("127.0.0.1", metrics_port, timeout=3)
    conn.request("GET", path)
    resp = conn.getresponse()
    body = resp.read().decode("utf-8", "replace")
    conn.close()
    return resp.status, body


def render(lines, path):
    """把 (文本, 颜色) 列表画成终端样式帧；文本超长自动折行。"""
    img = Image.new("RGB", (W, H), BG)
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT, 15)
    x, y = 18, 16
    for text, color in lines:
        for row in textwrap(text, 104):
            if y > H - 20:
                break
            draw.text((x, y), row, font=font, fill=color)
            y += 22
    img.save(path)


def textwrap(text, width):
    out = []
    for chunk in text.split("\n"):
        while len(chunk) > width:
            out.append(chunk[:width])
            chunk = chunk[width:]
        out.append(chunk)
    return out


def curl_probe(metrics_port):
    """真实跑一次 curl，取回它的输出串——报错文案也是实测的。"""
    proc = subprocess.run(
        ["curl", "-sS", "--max-time", "3", f"http://127.0.0.1:{metrics_port}/health"],
        capture_output=True,
        text=True,
        timeout=10,
    )
    return (proc.stdout + proc.stderr).strip()


def main():
    if not CHIRP_CHAT.exists():
        sys.exit(f"missing {CHIRP_CHAT}")
    OUT.mkdir(parents=True, exist_ok=True)
    used = set()
    metrics_port = pick_port(used)
    chat_port = pick_port(used)
    ws_port = pick_port(used)

    off_curl = curl_probe(metrics_port)
    if "Failed to connect" not in off_curl and "Connection refused" not in off_curl:
        sys.exit(f"unexpected off-run curl output: {off_curl!r}")
    if not port_dead(metrics_port):
        sys.exit("metrics port already listening before the off run")

    proc = start_chat([chat_port, ws_port])
    try:
        if not wait_port(chat_port):
            log = proc.stdout.read().decode("utf-8", "replace") if proc.poll() else ""
            sys.exit(f"off-run chirp_chat never came up on {chat_port}\n{log[-1500:]}")
        if not port_dead(metrics_port):
            sys.exit("off run: /metrics unexpectedly listening")
    finally:
        stop(proc)

    on_chat_port = pick_port(used)
    on_ws_port = pick_port(used)
    # 换一组端口再跑 on：上一个实例的端口可能还在 TIME_WAIT。
    proc = start_chat([on_chat_port, on_ws_port, "--metrics_port", str(metrics_port)])
    try:
        if not wait_port(on_chat_port):
            sys.exit(f"on-run chirp_chat never came up on {on_chat_port}")
        try:
            health_status, health_body = get_path(metrics_port, "/health")
            metrics_status, metrics_body = get_path(metrics_port, "/metrics")
        except OSError as exc:
            sys.exit(f"on run: endpoint unreachable: {exc}")
        if health_status != 200 or health_body.strip() != "OK":
            sys.exit(f"on run: /health unexpected (status={health_status} body={health_body!r})")
        if metrics_status != 200:
            sys.exit(f"on run: /metrics returned {metrics_status}")
    finally:
        stop(proc)

    if metrics_body.strip():
        samples = [ln for ln in metrics_body.splitlines() if not ln.startswith("#")]
        metric_lines = [(ln, TEXT) for ln in (samples or metrics_body.splitlines())[:5]]
        empty_note = []
    else:
        metric_lines = []
        # 实测为空就如实写出来——这是当前代码的真实行为，不是脚本的文案。
        empty_note = [
            ("# /metrics answers 200 with an EMPTY body: no service in the repo", TEXT),
            ("# registers a SetMetricsHandler, so the counters collected in", TEXT),
            ("# SimpleMetrics (e.g. chirp_chat_sessions) are never exported", TEXT),
        ]

    off_frame = [
        (f"$ chirp_chat --port {chat_port} --ws_port {ws_port}", PROMPT),
        (f"$ curl -sS --max-time 3 http://127.0.0.1:{metrics_port}/health", PROMPT),
        (f"# --metrics_port defaults to 0: nothing listens on {metrics_port}", TEXT),
        *[(ln, ERROR) for ln in textwrap(off_curl, 68)],
    ]
    on_frame = [
        (f"$ chirp_chat --port {on_chat_port} --ws_port {on_ws_port} --metrics_port {metrics_port}", PROMPT),
        (f"$ curl -sS --max-time 3 http://127.0.0.1:{metrics_port}/health", PROMPT),
        (health_body.strip(), TEXT),
        (f"$ curl -sS --max-time 3 http://127.0.0.1:{metrics_port}/metrics", PROMPT),
        (str(metrics_status), TEXT),
        *empty_note,
        *metric_lines,
    ]
    render(off_frame, OUT / f"{NAME}-off.png")
    render(on_frame, OUT / f"{NAME}-on.png")

    seq = OUT / f".seq-{NAME}"
    shutil.rmtree(seq, ignore_errors=True)
    seq.mkdir()
    shutil.copyfile(OUT / f"{NAME}-off.png", seq / "01.png")
    shutil.copyfile(OUT / f"{NAME}-on.png", seq / "02.png")
    subprocess.run(
        [
            "ffmpeg", "-y", "-loglevel", "error", "-framerate", "0.6",
            "-i", str(seq / "%02d.png"),
            "-filter_complex", "split[a][b];[a]palettegen[p];[b][p]paletteuse",
            "-loop", "0", str(OUT / f"{NAME}.gif"),
        ],
        check=True,
    )
    shutil.rmtree(seq)
    print(f"off: {off_curl.splitlines()[0] if off_curl.splitlines() else off_curl}")
    print(
        f"on: /health={health_body.strip()!r}，/metrics HTTP {metrics_status}，"
        f"正文 {len(metrics_body)} 字节"
    )
    print("DONE")


if __name__ == "__main__":
    main()
