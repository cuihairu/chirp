#!/usr/bin/env python3
"""演示账号自助脚本(部署腿用):对 auth 服务(默认 6000/TCP)注册演示账号,
再用 PASSWORD_LOGIN 复验,以密码登录成功为终判(幂等:已存在导致的注册失败
可容忍)。

零第三方依赖:手写 protobuf 编解码(只为这几个全字符串消息)+ u32be 帧
(对齐 libs/network/protobuf_framing.h:大端 4 字节长度前缀)。

用法:
  python3 demo_account.py --host app_auth --port 6000 \
      --username demo --password <pw> [--email <addr>]
"""
import argparse
import socket
import struct
import sys

# proto/gateway.proto:Packet{msg_id=1, sequence=2, body=3}
# MsgID:REGISTER_REQ=1008/REGISTER_RESP=1009,
#        PASSWORD_LOGIN_REQ=1010/PASSWORD_LOGIN_RESP=1011
REGISTER_REQ = 1008
REGISTER_RESP = 1009
PASSWORD_LOGIN_REQ = 1010
PASSWORD_LOGIN_RESP = 1011
OK = 0


def _varint(value: int) -> bytes:
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        out.append(b | (0x80 if value else 0))
        if not value:
            return bytes(out)


def _key(field: int, wire: int) -> bytes:
    return _varint((field << 3) | wire)


def _encode(fields: dict) -> bytes:
    """{field_no: [value, ...]} -> protobuf bytes;int 走 varint,bytes/str 走 LEN。"""
    out = bytearray()
    for field, values in fields.items():
        for value in values:
            if isinstance(value, int):
                out += _key(field, 0) + _varint(value)
            else:
                raw = value.encode("utf-8") if isinstance(value, str) else bytes(value)
                out += _key(field, 2) + _varint(len(raw)) + raw
    return bytes(out)


def _decode(buf: bytes) -> dict:
    """protobuf bytes -> {field_no: [value, ...]};varint 转 int,LEN 保留 bytes。"""
    out: dict = {}
    i = 0

    def read_varint() -> int:
        nonlocal i
        shift = 0
        val = 0
        while True:
            if i >= len(buf):
                raise ValueError("truncated varint")
            b = buf[i]
            i += 1
            val |= (b & 0x7F) << shift
            if not b & 0x80:
                return val
            shift += 7
            if shift > 70:
                raise ValueError("varint too long")

    while i < len(buf):
        key = read_varint()
        field, wire = key >> 3, key & 7
        if wire == 0:
            value = read_varint()
        elif wire == 1:
            value = buf[i : i + 8]
            i += 8
        elif wire == 2:
            length = read_varint()
            value = buf[i : i + length]
            i += length
        elif wire == 5:
            value = buf[i : i + 4]
            i += 4
        else:
            raise ValueError(f"unsupported wire type {wire}")
        out.setdefault(field, []).append(value)
    if i != len(buf):
        raise ValueError("trailing bytes")
    return out


def _frame_packet(msg_id: int, sequence: int, body: bytes) -> bytes:
    packet = _encode({1: [msg_id], 2: [sequence], 3: [body]})
    return struct.pack(">I", len(packet)) + packet


def _send(sock: socket.socket, msg_id: int, sequence: int, body: bytes) -> None:
    sock.sendall(_frame_packet(msg_id, sequence, body))


def _recv(sock: socket.socket) -> dict:
    """读一帧 -> Packet 解码结果(body 已按 protobuf 解出)。"""
    header = b""
    while len(header) < 4:
        chunk = sock.recv(4 - len(header))
        if not chunk:
            raise ConnectionError("connection closed while reading frame header")
        header += chunk
    (length,) = struct.unpack(">I", header)
    payload = b""
    while len(payload) < length:
        chunk = sock.recv(length - len(payload))
        if not chunk:
            raise ConnectionError("connection closed while reading frame body")
        payload += chunk
    packet = _decode(payload)
    body = packet.get(3, [b""])[0]
    return {"msg_id": packet.get(1, [0])[0], "body": _decode(body)}


def _text(fields: dict, field: int) -> str:
    values = fields.get(field, [])
    return values[0].decode("utf-8", "replace") if values else ""


def main() -> int:
    parser = argparse.ArgumentParser(description="chirp 演示账号注册+密码登录复验")
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=6000)
    parser.add_argument("--username", required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--email", default="")
    parser.add_argument("--display-name", default="")
    args = parser.parse_args()

    sock = socket.create_connection((args.host, args.port), timeout=15)
    sock.settimeout(15)
    try:
        # 1) 注册:已存在会回 INVALID_PARAM(error_message="Username already
        #    exists"),不判死,终判看第 2 步。
        register = _encode(
            {
                1: [args.username],
                2: [args.email],
                3: [args.password],
                4: [args.display_name or args.username],
            }
        )
        _send(sock, REGISTER_REQ, 1, register)
        resp = _recv(sock)
        if resp["msg_id"] != REGISTER_RESP:
            print(f"unexpected msg_id {resp['msg_id']} for register", file=sys.stderr)
            return 1
        code = resp["body"].get(1, [1])[0]
        message = _text(resp["body"], 4)
        if code == OK:
            print(f"register: ok user_id={_text(resp['body'], 2)}")
        else:
            print(f"register: not created (code={code} message={message})")

        # 2) 密码登录复验:成功即账号可用。
        login = _encode(
            {
                1: [args.username],
                2: [args.password],
                3: ["demo-setup"],
                4: ["web"],
            }
        )
        _send(sock, PASSWORD_LOGIN_REQ, 2, login)
        resp = _recv(sock)
        if resp["msg_id"] != PASSWORD_LOGIN_RESP:
            print(f"unexpected msg_id {resp['msg_id']} for password login", file=sys.stderr)
            return 1
        code = resp["body"].get(1, [1])[0]
        if code != OK:
            print(
                f"password login: FAILED code={code} message={_text(resp['body'], 11)}",
                file=sys.stderr,
            )
            return 1
        print(
            "password login: ok "
            f"user_id={_text(resp['body'], 2)} username={_text(resp['body'], 3)}"
        )
        return 0
    finally:
        sock.close()


if __name__ == "__main__":
    sys.exit(main())
