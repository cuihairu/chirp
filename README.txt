chirp daily build
=================
stamp:  20261010-ae6907d
commit: ae6907d6d3ccf034de9970b9a5d159bc4cc73d6d
built:  2026-10-10 23:20 UTC (GitHub Actions nightly)

内容:
  cpp/<arch>/chirp-cpp-sdk-<arch>.tar.gz   C++ core SDK(库+头+proto 源,Linux 含 crashpad_handler)
  desktop/<arch>/chirp-desktop-<arch>.*    桌面聊天 App
      linux: .deb / darwin: .dmg / windows: -setup.exe
  symbols/<arch>/chirp-cpp-symbols-<arch>.tar.gz
                                           C++ 符号包(breakpad .sym,崩溃栈符号化用,独立成件)
  go/chirp-go-sdk.tar.gz                   Go 服务端 SDK 源码包
  ts/chirp-protocol.tgz                    @chirp/protocol npm 包
  manifest.json                            可用面清单(一键安装读它)
  SHA256SUMS                               全部资产 sha256(sha256sum -c)

分发(两路):
  - 本 run 页 -> Artifacts -> daily-build(固定名,覆盖旧一份);
  - nightly-dist 分支镜像(本树的逐文件直链,匿名可下):
    https://github.com/cuihairu/chirp/tree/nightly-dist
  一键安装(仓库根脚本,自动选平台产物):
    curl -fsSL https://raw.githubusercontent.com/cuihairu/chirp/main/install.sh | bash
    irm https://raw.githubusercontent.com/cuihairu/chirp/main/install.ps1 | iex
警示: 全部产物未签名。打包前的验证 = C++ SDK 目标编译 +
go vet/build + @chirp/protocol 测试套件 + 桌面 App Tauri 构建;完整门禁以仓库 CI 为准。
