#!/usr/bin/env python3
"""配置开关效果证据采集（服务端 CLI 旗标）：off/on 两次真跑 + 终端样式帧 + GIF。

覆盖口径：能不带凭据、不依赖外部网络就证明「开≠关」的旗标。当前清单：

  --metrics_port <port>   默认 0=不监听；给端口后 libs/common 的 MetricsHttpServer 起来。
                          off = 该端口拒连；on = GET /health 200 "OK"、GET /metrics 200。
                          实测发现 /metrics 正文是空的：仓内没有任何服务注册
                          SetMetricsHandler（只有 tests/unit/common_metrics_test.cc 调过），
                          计数确实在收（services/shared/chat/src/enhanced_dispatch.cc 有
                          CHIRP_GAUGE_INC("chirp_chat_sessions")）但从未导出。
  --allow_scaffold_login  默认 0。1 = 允许「token 即用户 ID」的开发脚手架登录；
                          0 = token 既不是有效 access token 也不是活跃 session 就拒绝。
                          用裸 TCP 客户端直连 auth 发 LOGIN_REQ 取证（零第三方依赖，
                          编解码照 libs/network/protobuf_framing.h 的 u32be 长度前缀，
                          与 scripts/demo_account.py 同手法）。off = code 3 AUTH_FAILED，
                          on = code 0 且 user_id 回填成 token。

未覆盖（同页「开关清单」表里逐条写了缺什么）：--push_transport / --apns-* 这组要一个把
chirp_app_notification 接进栈的 rig（桌面 dev 栈没起这个服务，gateway 侧还要配
--notification_host/--notification_port/--notification_secret），真投递又卡在 FCM/APNs
凭据上（台账 L25）。

取证形态：本机没有 xterm/asciinema，录不了真终端录屏。帧是把两次真实运行的实测输出渲染
成终端样式再用 ffmpeg 缝合——帧内每一行都是实测拿回来的文本（连 curl 的报错串都是真的）。

用法：python3 scripts/capture_cli_switch_effects.py
产物：docs/screenshots/switches/cli-{metrics-port,scaffold-login}-{off,on}.png 与 .gif
"""

import http.client
import os
import signal
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from switch_frames import (ERROR, PROMPT, TEXT, render_frame, stitch_gif)  # noqa: E402

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "docs/screenshots/switches"

# ---- proto/gateway.proto: Packet{msg_id=1, sequence=2, body=3} ----
LOGIN_REQ, LOGIN_RESP = 1003, 1004
# ---- proto/common.proto ----
OK, AUTH_FAILED = 0, 3
CODE_NAMES = {OK: "OK", 1: "INTERNAL_ERROR", 2: "INVALID_PARAM",
             3: "AUTH_FAILED", 4: "SESSION_EXPIRED", 5: "USER_NOT_FOUND"}


def _varint(value):
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        out.append(b | (0x80 if value else 0))
        if not value:
            return bytes(out)


def _tag(field, wire):
    return _varint((field << 3) | wire)


def _sfield(field, text):
    raw = text.encode("utf-8")
    return _tag(field, 2) + _varint(len(raw)) + raw


def login_request(token):
    # proto/auth.proto: LoginRequest{token=1, device_id=2, platform=3, ...}
    return _sfield(1, token)


def packet(msg_id, sequence, body):
    return (_tag(1, 0) + _varint(msg_id) + _tag(2, 0) + _varint(sequence)
            + _tag(3, 2) + _varint(len(body)) + body)


def _read_varint(buf, pos):
    value = shift = 0
    while True:
        byte = buf[pos]
        pos += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, pos
        shift += 7


def decode_packet(raw):
    """只解我们要的 msg_id/sequence/body（字段号 1/2/3）。"""
    fields, pos = {}, 0
    while pos < len(raw):
        key, pos = _read_varint(raw, pos)
        field, wire = key >> 3, key & 7
        if wire == 0:
            value, pos = _read_varint(raw, pos)
        elif wire == 2:
            length, pos = _read_varint(raw, pos)
            value = raw[pos:pos + length]
            pos += length
        else:
            return None
        fields.setdefault(field, []).append(value)
    return fields


def scaffold_probe(port, token="uia", timeout=4.0):
    """裸 TCP 连 auth，发一个 LOGIN_REQ，回读 LOGIN_RESP。零依赖。

    帧 = u32be 长度前缀 + Packet{msg_id=1003, sequence=1, body=LoginRequest{token}}，
    与 libs/network/protobuf_framing.h / demo_account.py 同格式。
    """
    sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    try:
        raw = packet(LOGIN_REQ, 1, login_request(token))
        sock.sendall(struct.pack(">I", len(raw)) + raw)
        header = _recv_exact(sock, 4, timeout)
        (length,) = struct.unpack(">I", header)
        fields = decode_packet(_recv_exact(sock, length, timeout))
        return fields
    finally:
        sock.close()


def _recv_exact(sock, count, timeout):
    sock.settimeout(timeout)
    buf = b""
    while len(buf) < count:
        chunk = sock.recv(count - len(buf))
        if not chunk:
            raise RuntimeError(f"connection closed after {len(buf)}/{count} bytes")
        buf += chunk
    return buf


def pick_port(used):
    """bind(0) 取空闲端口；记进 used 避免同一轮重复挑到同一个。"""
    for _ in range(20):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
        sock.close()
        if port not in used:
            used.add(port)
            return port
    raise RuntimeError("no free port")


def start(binary, args):
    return subprocess.Popen([str(binary), *args], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, start_new_session=True)


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
        sock = socket.socket()
        sock.settimeout(0.5)
        try:
            if sock.connect_ex(("127.0.0.1", port)) == 0:
                return True
        finally:
            sock.close()
        time.sleep(0.25)
    return False


def port_dead(port):
    sock = socket.socket()
    sock.settimeout(1.0)
    try:
        return sock.connect_ex(("127.0.0.1", port)) != 0
    finally:
        sock.close()


# ---------------------------------------------------------------- 开关 1
def capture_metrics(used):
    metrics_port = pick_port(used)
    if not port_dead(metrics_port):
        sys.exit("metrics port already listening before the off run")

    probe = subprocess.run(["curl", "-sS", "--max-time", "3",
                           f"http://127.0.0.1:{metrics_port}/health"],
                          capture_output=True, text=True, timeout=10)
    off_curl = (probe.stdout + probe.stderr).strip()
    if "Failed to connect" not in off_curl:
        sys.exit(f"unexpected off-run curl output: {off_curl!r}")

    chat_port, ws_port = pick_port(used), pick_port(used)
    proc = start(REPO / "build/services/shared/chat/chirp_chat",
                 ["--port", str(chat_port), "--ws_port", str(ws_port)])
    try:
        if not wait_port(chat_port):
            sys.exit(f"off-run chirp_chat never came up on {chat_port}")
        if not port_dead(metrics_port):
            sys.exit("off run: /metrics unexpectedly listening")
    finally:
        stop(proc)

    on_chat, on_ws = pick_port(used), pick_port(used)
    proc = start(REPO / "build/services/shared/chat/chirp_chat",
                 ["--port", str(on_chat), "--ws_port", str(on_ws),
                  "--metrics_port", str(metrics_port)])
    health = metrics_body = None
    try:
        if not wait_port(on_chat):
            sys.exit(f"on-run chirp_chat never came up on {on_chat}")
        health = _http_get(metrics_port, "/health")
        metrics_body = _http_get(metrics_port, "/metrics")[1]
    finally:
        stop(proc)
    if health != (200, b"OK"):
        sys.exit(f"on run: /health answered {health!r}, expected (200, b'OK')")
    return off_curl, metrics_body


def _http_get(port, path):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
    conn.request("GET", path)
    resp = conn.getresponse()
    status, body = resp.status, resp.read()
    conn.close()
    return status, body


# ---------------------------------------------------------------- 开关 2
def capture_scaffold(used):
    auth = REPO / "build/services/app/auth/chirp_app_auth"
    results = {}
    for flag, label in (([], "off"), (["--allow_scaffold_login", "1"], "on")):
        port = pick_port(used)
        proc = start(auth, ["--port", str(port), "--jwt_secret", "dev_secret", *flag])
        try:
            if not wait_port(port):
                sys.exit(f"auth ({label}) never came up on {port}")
            fields = scaffold_probe(port)
            if not fields or 3 not in fields:
                sys.exit(f"scaffold ({label}): no LOGIN_RESP body in the reply")
            results[label] = fields
        finally:
            stop(proc)
    return results


def login_fields_of(fields):
    """从 Packet 的 body 里解 LoginResponse{code=1, session_id=2, user_id=4, ...}。

    返回 (code, session_id, user_id)。code 缺省按 protobuf3 规则视为 0（=OK，
    零值不写盘）；session_id/user_id 缺省为 None。
    """
    body = fields[3][-1]
    code, session_id, user_id = 0, None, None
    pos = 0
    while pos < len(body):
        key, pos = _read_varint(body, pos)
        field, wire = key >> 3, key & 7
        if wire == 0:
            value, pos = _read_varint(body, pos)
            if field == 1:
                code = value
        elif wire == 2:
            length, pos = _read_varint(body, pos)
            value = body[pos:pos + length].decode("utf-8", "replace")
            pos += length
            if field == 2:
                session_id = value
            elif field == 4:
                user_id = value
        else:
            break
    return code, session_id, user_id


def main():
    if not (REPO / "build/services/shared/chat/chirp_chat").exists():
        sys.exit("missing build/services/shared/chat/chirp_chat")
    if not (REPO / "build/services/app/auth/chirp_app_auth").exists():
        sys.exit("missing build/services/app/auth/chirp_app_auth")
    OUT.mkdir(parents=True, exist_ok=True)
    used = set()

    # ---- 开关 1：--metrics_port ----
    off_curl, metrics_body = capture_metrics(used)
    render_frame([
        ("$ chirp_chat --port <p> --ws_port <w>   # 不带 --metrics_port", PROMPT),
        ("$ curl -sS http://127.0.0.1:<m>/health", PROMPT),
        (off_curl, ERROR),
        ("# 缺省 0：MetricsHttpServer 根本不起，端口上什么都没有", TEXT),
    ], OUT / "cli-metrics-port-off.png")
    render_frame([
        ("$ chirp_chat --port <p> --ws_port <w> --metrics_port <m>", PROMPT),
        ("$ curl -sS http://127.0.0.1:<m>/health", PROMPT),
        ("OK", TEXT),
        ("$ curl -sS http://127.0.0.1:<m>/metrics", PROMPT),
        (f"HTTP 200, body {len(metrics_body)} bytes (empty: no service registers "
         "SetMetricsHandler)", TEXT),
    ], OUT / "cli-metrics-port-on.png")
    stitch_gif(OUT / "cli-metrics-port-off.png", OUT / "cli-metrics-port-on.png",
               OUT / "cli-metrics-port.gif")
    print(f"metrics off: {off_curl}")
    print(f"metrics on : /health 200 OK, /metrics body {len(metrics_body)} bytes")

    # ---- 开关 2：--allow_scaffold_login ----
    results = capture_scaffold(used)
    off_fields, on_fields = results["off"], results["on"]
    off_code, off_session, off_user = login_fields_of(off_fields)
    on_code, on_session, on_user = login_fields_of(on_fields)
    if off_code != AUTH_FAILED:
        sys.exit(f"off run: expected AUTH_FAILED(3), got {off_code!r}")
    if off_session is not None or off_user is not None:
        sys.exit(f"off run: refused login should carry no session/user, got "
                 f"{off_session!r}/{off_user!r}")
    if on_code != OK or on_session != "uia_sess" or on_user != "uia":
        sys.exit(f"on run: expected OK+session_id=uia_sess+user_id=uia, got "
                 f"{on_code!r}/{on_session!r}/{on_user!r}")

    render_frame([
        ("$ chirp_app_auth --port <p> --jwt_secret dev_secret", PROMPT),
        ("$ probe: LOGIN_REQ(1003) body=LoginRequest{token:\"uia\"}", PROMPT),
        (f"LOGIN_RESP(1004) code={CODE_NAMES.get(off_code, off_code)} ({off_code})", ERROR),
        ("# --allow_scaffold_login defaults to 0: token is neither a valid access", TEXT),
        ("# token nor a live session, so the login is refused outright", TEXT),
    ], OUT / "cli-scaffold-login-off.png")
    render_frame([
        ("$ chirp_app_auth --port <p> --jwt_secret dev_secret --allow_scaffold_login 1", PROMPT),
        ("$ probe: LOGIN_REQ(1003) body=LoginRequest{token:\"uia\"}", PROMPT),
        (f"LOGIN_RESP(1004) code={CODE_NAMES.get(on_code, on_code)} ({on_code})", TEXT),
        (f"session_id=\"{on_session}\"", TEXT),
        (f"user_id=\"{on_user}\"", TEXT),
        ("# same probe, same token: with the flag on, the login lands and", TEXT),
        ("# user_id echoes the token", TEXT),
    ], OUT / "cli-scaffold-login-on.png")
    stitch_gif(OUT / "cli-scaffold-login-off.png", OUT / "cli-scaffold-login-on.png",
               OUT / "cli-scaffold-login.gif")

    print(f"scaffold off: code={off_code} ({CODE_NAMES.get(off_code, off_code)})")
    print(f"scaffold on : code={on_code} ({CODE_NAMES.get(on_code, on_code)}) user_id={on_user}")
    print("DONE")


if __name__ == "__main__":
    main()
