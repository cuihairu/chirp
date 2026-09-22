# 集成测试修复总结

## 概览

集成测试的搭建围绕当前仓库现状做了稳定化:

- 集成目标干净构建
- 默认测试路径跑一次 protobuf/帧格式冒烟检查
- 可选的连接路径做一次真实的网关登录冒烟
- 包装脚本现在可以脱离 Docker 直接拉起本地 `auth` 和 `gateway` 二进制

## 当前行为

`tests/integration/integration_test.cc` 现在提供:

1. `Protobuf Encoding`
2. 带 `--connect` 时的 `Basic Connection`

连接测试:

- 打开到网关的 TCP 连接
- 发送 `LOGIN_REQ`
- 等待 `LOGIN_RESP`
- 失败时以非零码退出

## 当前入口

默认冒烟:

```bash
bash tests/run_integration_tests.sh
```

Docker 支撑的连接冒烟:

```bash
bash tests/run_integration_tests.sh --docker --connect
```

本地二进制的连接冒烟:

```bash
bash tests/run_integration_tests.sh --local-services --gateway-port 5500 --auth-port 6500
```

## 脚本改动

`tests/run_integration_tests.sh` 现在:

- 优先复用已经能用的系统依赖
- 不强制 `vcpkg install`
- 明确想要该行为时接受 `--use-vcpkg`
- 可以启动 Docker 服务
- 可以启动本地 `chirp_auth` 和 `chirp_gateway`
- 退出时清理本地子进程

## 备注

这仍然是冒烟覆盖,不是对全部 chat、social、voice 流程的服务间完整验证。
