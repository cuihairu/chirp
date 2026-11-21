#!/usr/bin/env python3
"""生成「本仓全部开关」清单表（docs 配置效果页的清单段）。

枚举口径（2026-11-21 定）：「开启后有效果」的**离散**开关——GUI 上的开关控件，以及
服务端 CLI 里取值是离散枚举（on/off、0/1、logging|http、replace|reject|record）的旗标。
不在此口径内、因此在表里明确排除的：数值调优旋钮（限流/TTL/上限/超时/重试次数）、
连接端点（host/port）、凭据类（secret/token/password/key）、编译期与仅测试用的开关。

脚本的职责是**防止清单漂移**：每一行都回源码核对——GUI 行核对 data-testid 仍在，
CLI 行核对旗标串仍在、且缺省值与源码写的一致。核对不过就非零退出、不写文件。

用法：
  python3 scripts/build_config_switch_inventory.py            # 只打印 markdown
  python3 scripts/build_config_switch_inventory.py --write    # 同时写回两页文档
"""

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DOCS = [(REPO / "docs/guide/config-effects.md", False),
        (REPO / "docs/zh/guide/config-effects.md", True)]
BEGIN = "<!-- switch-inventory:begin -->"
END = "<!-- switch-inventory:end -->"

# (ref, 名称, 所在服务/面, 源码相对路径, 缺省值, 分类, 未覆盖原因, 缺省值原文探针)
# GUI 行的 ref 是 data-testid；CLI 行的 ref 是旗标名（不带 --）。
# 第八/九项分别是中文/英文的未覆盖原因；最后一项给「缺省值不是 ParseArg 形参、而是独立赋值语句」的旗标用。
SWITCHES = [
    # ---- GUI：桌面端 ----
    ("desktop-notify", "桌面通知", "桌面端", "apps/desktop/src/ui/MainWindow.tsx", "—", "gui", "", "", ""),
    ("friend-online-notify", "好友上线通知", "桌面端", "apps/desktop/src/ui/MainWindow.tsx", "—", "gui", "", "", ""),
    ("dm-restriction", "私聊限制", "桌面端", "apps/desktop/src/ui/MainWindow.tsx", "—", "gui", "", "", ""),
    # ---- GUI：web 端 ----
    ("notify-enable", "桌面通知（文案从「开启桌面通知」翻成「桌面通知已开启」）", "web 端",
     "apps/web_companion/src/pages/ChatPage.tsx", "—", "gui", "", "", ""),
    ("friend-online-notify-switch", "好友上线通知", "web 端",
     "apps/web_companion/src/components/FriendsDialog.tsx", "—", "gui", "", "", ""),
    ("dm-restriction-switch", "私聊限制", "web 端",
     "apps/web_companion/src/components/FriendsDialog.tsx", "—", "gui", "", "", ""),
    ("game-presence-toggle", "游戏在线状态", "web 端",
     "apps/web_companion/src/components/OnlineDevicesDialog.tsx", "—", "gui", "", "", ""),
    # ---- CLI：离散枚举旗标 ----
    ("metrics_port", "指标端点（0=不监听，给端口则监听）", "chirp_chat 等 10 个长驻服务",
     "services/shared/chat/src/main_enhanced.cc", "0", "covered", "", "", ""),
    ("allow_unknown_peers", "白名单外的 peer 注册是否放行（开放注册）", "chirp_chat",
     "services/shared/chat/src/main_enhanced.cc", "0", "covered", "", "", ""),
    ("hub_mode", "是否开出 game-chat peer 接入器", "chirp_chat",
     "services/shared/chat/src/main_enhanced.cc", "0", "covered", "", "", ""),
    ("enable_migration", "存储迁移是否开启", "chirp_chat（增强版）",
     "services/shared/chat/src/main_enhanced.cc", "1", "covered", "", "", ""),
    ("broker_consumer", "是否为 broker 消费者", "chirp_server_gateway",
     "services/game/server_gateway/src/main.cc", "(空=关)", "cli-needs-rig",
     "需要 broker 流 + 双消费实例", "needs the broker stream plus two consumer instances", ""),
    ("kick_previous", "新登录是否顶掉同账号旧会话", "chirp_app_auth",
     "services/app/auth/src/main_enhanced.cc", "1", "covered", "", "", ""),
    ("allow_scaffold_login", "是否允许「用户 ID 即凭据」的脚手架登录", "chirp_app_auth",
     "services/app/auth/src/main_enhanced.cc", "0", "covered", "", "", ""),
    ("word_filter_policy", "敏感词处理策略 replace|reject|record", "chirp_chat",
     "services/shared/chat/src/main_enhanced.cc", "replace", "covered-3state", "", "", ""),
    ("asr_disposition", "ASR 处置策略 flag|block", "chirp_voice",
     "services/voice/src/main.cc", "flag", "cli-needs-toolchain",
     "整条 ASR 链要 ASR 引擎与 --asr_lexicon 才在位", "the whole ASR chain needs the ASR engine and --asr_lexicon in place", ""),
    ("push_transport", "推送传输 logging|http", "chirp_app_notification",
     "services/app/notification/src/main.cc", "logging", "cli-credential",
     "dev 栈没起这个服务；真投递又卡在 FCM/APNs 凭据", "the dev stack does not run that service; real egress is also gated on FCM/APNs credentials",
     'push_transport_flag = "logging"'),
    ("push_verify_tls", "http 传输是否校验证书", "chirp_app_notification",
     "services/app/notification/src/main.cc", "on", "cli-credential",
     "同上，要有一次真实推送出向", "same as above: needs one real outbound push", "push_verify_tls = true;"),
    ("apns-sandbox", "APNs 走开发端点", "chirp_app_notification",
     "services/app/notification/src/main.cc", "(不传=生产端点)", "cli-credential",
     "台账 L25：缺 .p8 / Team ID / Key ID / Bundle ID", "ledger L25: missing the .p8 / Team ID / Key ID / Bundle ID", ""),
]

# 「所在服务/面」列只有几个值是中文的，这里给英文页换成英文；其余（服务名本身）两种语言通用。
WHERE_EN = {
    "桌面端": "desktop app",
    "web 端": "web companion",
    "chirp_chat 等 10 个长驻服务": "chirp_chat and the other long-lived services",
    "chirp_chat（增强版）": "chirp_chat (enhanced)",
}

COVERAGE = {
    "gui": ("✅ 有 off/on 图+GIF", "✅ off/on + GIF"),
    "covered": ("✅ 有 off/on 图+GIF", "✅ off/on + GIF"),
    "covered-3state": ("✅ 有 3 态图+GIF（replace/reject/record）",
                       "✅ 3-state off/mid/on + GIF"),
    "cli-needs-rig": ("未覆盖：要自建服务实例/双实例对照", "not covered: needs a scripted service rig"),
    "cli-credential": ("未覆盖：要 FCM/APNs 凭据", "not covered: needs provider credentials"),
    "cli-needs-toolchain": ("未覆盖：要 ASR 引擎", "not covered: needs the ASR engine"),
}


def check_gui(testid, rel):
    text = (REPO / rel).read_text(encoding="utf-8")
    if f'data-testid="{testid}"' not in text:
        return f"GUI testid {testid} 不在 {rel}"
    return None


def check_cli(flag, rel, default, probe):
    text = (REPO / rel).read_text(encoding="utf-8")
    if f'"--{flag}"' not in text:
        return f"旗标 --{flag} 不在 {rel}"
    if default.startswith("("):        # 语义缺省（presence 旗标），不核对字面值
        return None
    if probe:                          # 缺省值写在独立赋值语句里
        return None if probe in text else f"{probe!r} 不在 {rel}（--{flag} 缺省值漂移？）"
    # 源码形态：ParseIntArg(..., "--flag", 0) / GetArg(..., "--flag", "replace")。
    pat = re.compile(r'"--' + re.escape(flag) + r'"\s*,\s*("([^"]*)"|(\d+))\s*\)')
    found = {m.group(2) if m.group(2) is not None else m.group(3)
             for m in pat.finditer(text)}
    if default not in found:
        return f"--{flag} 缺省值漂移：清单 {default} vs 源码 {sorted(found)}（{rel}）"
    return None


def render(rows, zh):
    head = (["开关", "所在服务/面", "缺省", "覆盖情况"] if zh
            else ["Switch", "Service / plane", "Default", "Coverage"])
    out = ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    for ref, _name, where, _rel, default, cls, why, why_en, _probe in rows:
        where = WHERE_EN.get(where, where) if not zh else where
        cov = COVERAGE[cls][0 if zh else 1]
        text = why if zh else why_en
        if text:
            cov += f"（{text}）" if zh else f" — {text}"
        out.append(f"| `{ref}` | {where} | {default} | {cov} |")
    return "\n".join(out)


def write_back(rows):
    for path, is_zh in DOCS:
        doc = path.read_text(encoding="utf-8")
        if BEGIN not in doc or END not in doc:
            raise SystemExit(f"{path} 缺 {BEGIN}/{END} 标记，先手动加段落骨架")
        head, rest = doc.split(BEGIN, 1)
        _rest, tail = rest.split(END, 1)
        path.write_text(head + BEGIN + "\n" + render(rows, is_zh) + "\n\n" + END + tail,
                        encoding="utf-8")
        print("wrote", path.relative_to(REPO))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    errors = []
    for ref, _n, _w, rel, default, cls, _why, _why_en, probe in SWITCHES:
        err = (check_gui(ref, rel) if cls == "gui"
               else check_cli(ref, rel, default, probe))
        if err:
            errors.append(err)
    if errors:
        print("清单核对失败：", file=sys.stderr)
        for e in errors:
            print(" -", e, file=sys.stderr)
        sys.exit(1)
    print(render(SWITCHES, False))
    print(f"\n# {len(SWITCHES)} switches verified against source")
    if args.write:
        write_back(SWITCHES)


if __name__ == "__main__":
    main()
