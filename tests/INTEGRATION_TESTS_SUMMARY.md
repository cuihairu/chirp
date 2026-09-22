# 集成测试现状

## 已验证的内容

仓库目前有两个经过验证的层:

- 经 `ctest --test-dir build --output-on-failure` 的单元测试
- `tests/integration` 下可构建的集成测试目标

集成可执行文件目前保证一次本地冒烟检查:

- protobuf 编解码
- 网关包帧格式
- 与工程库的链接

它还有一条可选的活连接路径:

- 打开到网关的 TCP 连接
- 发送 `LOGIN_REQ`
- 期待成功的 `LOGIN_RESP`

这条路径默认不跑。服务可以两种方式提供:

- Docker:`--docker --connect`
- 本地二进制:`--local-services`

## 支持的命令

本地冒烟:

```bash
cmake -S . -B build
cmake --build build -j4
bash tests/run_integration_tests.sh
```

对活服务做连接冒烟:

```bash
docker compose up -d redis auth gateway chat social
bash tests/run_integration_tests.sh --docker --connect
```

用本地二进制做连接冒烟:

```bash
cmake -S . -B build
cmake --build build -j4 --target chirp_auth chirp_gateway
bash tests/run_integration_tests.sh --local-services --gateway-port 5500 --auth-port 6500
```

## 脚本行为

`tests/run_integration_tests.sh` 现在默认采用最不意外的行为:

- 复用主工程构建
- 系统依赖已可用时直接用
- 不强制 `vcpkg install`
- 确实想要依赖引导时接受 `--use-vcpkg`
- 可以不经 Docker 编排一条本地 auth/gateway 冒烟路径

## 范围说明

目前应把集成框架理解为冒烟覆盖,而不是每个服务的全面端到端覆盖。

今天已有的:

- `tests/integration/integration_test.cc` 可构建可运行
- `tests/integration/CMakeLists.txt` 链接仓库库
- `tests/integration/demo_test_framework.sh` 记录了真实入口

仍需随时间深入验证的:

- 对活服务的聊天投递流程
- 对活服务的 social/在线状态流程
- notification、search 等较新的服务面
- SDK 超出"能编译"之外的行为
