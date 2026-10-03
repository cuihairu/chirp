#!/usr/bin/env bash
# chirp 一键安装（Linux / macOS）
#
# 从每日构建镜像分支（nightly-dist，匿名可直链下载）拉取与本机 OS/架构
# 匹配的产物并安装。组件四选：
#   app   桌面聊天 App（Linux: .deb，root 走 dpkg -i，无 root 解包进 ~/.local；
#         macOS: .dmg 挂载拷贝 .app 进 /Applications，无权限时 ~/Applications）
#   cpp   C++ core SDK（库 + 头 + proto 源，装进 --prefix）
#   go    Go 服务端 SDK 源码包
#   ts    @chirp/protocol npm 包
#
# 用法:
#   # Linux / macOS 一键（默认装桌面 App，自动检测 OS 与架构）:
#   curl -fsSL https://raw.githubusercontent.com/cuihairu/chirp/main/install.sh | bash
#
#   # 带参数（装 C++ SDK 到自定义前缀）:
#   curl -fsSL https://raw.githubusercontent.com/cuihairu/chirp/main/install.sh | \
#     bash -s -- --component cpp --prefix ~/.local
#
#   # 本地执行:
#   ./install.sh [--component app|cpp|go|ts|all] [选项]
#
# 说明:
#   - 幂等：重跑即升级（覆盖产物；deb 重复安装即替换）。
#   - 失败即停：任何一步失败明确报错退出，不留半装状态。
#   - 平台面由每日构建矩阵决定（见 nightly.yml）：当前 Linux x86_64/aarch64、
#     macOS arm64（darwin-arm64）、Windows x64（走 install.ps1）；darwin-x64
#     与 windows-arm64 无构建腿，脚本会按 manifest 明确报错。
#   - chirp 每日构建无常驻服务可注册（服务端不在产物内），故无
#     --with-service 选项；需要服务化部署请走仓库根 docker-compose。
# 兼容 macOS 自带 bash 3.2（无关联数组等 4.0 特性；manifest 为单行 JSON，
# 用 sed 解析，不依赖 python）。
set -euo pipefail

MIRROR_DEFAULT="https://raw.githubusercontent.com/cuihairu/chirp/refs/heads/nightly-dist"

info() { printf '%s\n' "$*"; }
warn() { printf '[警告] %s\n' "$*" >&2; }
die()  { printf '错误: %s\n' "$*" >&2; exit 1; }

usage() {
	cat <<'EOF'
chirp 一键安装（Linux / macOS）

选项:
  --component C     安装组件: app（默认）/ cpp / go / ts / all
  --install-dir DIR go/ts 源码包安装目录（默认 ~/.local/share/chirp/<组件>）
  --prefix DIR      C++ SDK 前缀（默认 /usr/local，无写权限时 ~/.local）
  --mirror URL      每日构建镜像根地址（默认 nightly-dist 分支 raw 直链）
  -h, --help        显示本帮助

环境变量（curl | bash 管道形态无法传参时使用）:
  CHIRP_COMPONENT / CHIRP_INSTALL_DIR / CHIRP_PREFIX / CHIRP_MIRROR

重跑即升级（幂等）。go/ts 为源码包，装完打印接入方式；C++ SDK 装完
以 nm 符号探针验证；桌面 App 装完以包元数据（macOS 为 .app 落位）验证。
EOF
}

# ---------- 参数与环境变量 ----------
COMPONENT="${CHIRP_COMPONENT:-app}"
INSTALL_DIR="${CHIRP_INSTALL_DIR:-}"
PREFIX="${CHIRP_PREFIX:-}"
MIRROR="${CHIRP_MIRROR:-$MIRROR_DEFAULT}"

while [ $# -gt 0 ]; do
	case "$1" in
	--component)
		[ $# -ge 2 ] || die "--component 需要一个参数"
		COMPONENT="$2"
		shift 2
		;;
	--install-dir)
		[ $# -ge 2 ] || die "--install-dir 需要一个目录参数"
		INSTALL_DIR="$2"
		shift 2
		;;
	--prefix)
		[ $# -ge 2 ] || die "--prefix 需要一个目录参数"
		PREFIX="$2"
		shift 2
		;;
	--mirror)
		[ $# -ge 2 ] || die "--mirror 需要一个 URL 参数"
		MIRROR="$2"
		shift 2
		;;
	-h | --help)
		usage
		exit 0
		;;
	*)
		die "未知参数: $1（-h 查看用法）"
		;;
	esac
done

case "$COMPONENT" in
app | cpp | go | ts | all) ;;
*)
	die "未知组件: $COMPONENT —— 可选 app / cpp / go / ts / all"
	;;
esac

# ---------- root 提权辅助（非交互：只接受免密 sudo） ----------
as_root() {
	if [ "$(id -u)" -eq 0 ]; then
		"$@"
	elif command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
		sudo -n "$@"
	else
		return 127
	fi
}

# ---------- 下载工具 ----------
fetch() {
	# fetch <url> <输出文件>
	if command -v curl >/dev/null 2>&1; then
		curl -fsSL --retry 3 --connect-timeout 15 -o "$2" "$1"
	elif command -v wget >/dev/null 2>&1; then
		wget -q --tries=3 -O "$2" "$1"
	else
		die "需要 curl 或 wget 之一来下载产物，请先安装"
	fi
}

# ---------- OS / 架构检测 ----------
OS_RAW="$(uname -s)"
ARCH_RAW="$(uname -m)"

case "$OS_RAW" in
Linux) OS="linux" ;;
Darwin) OS="darwin" ;;
*)
	die "不支持的操作系统: $OS_RAW —— 本脚本支持 Linux 与 macOS；Windows 请用 install.ps1"
	;;
esac

# 产物目标名 = <os>-<arch>（与 nightly-dist 镜像目录一一对应）
case "$ARCH_RAW" in
x86_64 | amd64) ARCH="x64" ;;
aarch64 | arm64) ARCH="arm64" ;;
armv7l | armv8l | armhf | arm)
	die "不支持的架构: $ARCH_RAW（armv7）—— 每日构建矩阵当前为 x86_64/aarch64，如确需请提 issue"
	;;
i386 | i486 | i586 | i686 | x86)
	die "不支持的架构: $ARCH_RAW（32 位 x86）—— 每日构建未提供 32 位产物"
	;;
*)
	die "不支持的架构: $ARCH_RAW —— 已支持: x86_64、aarch64/arm64；矩阵面见 nightly.yml 与 nightly-dist/manifest.json"
	;;
esac
TARGET="${OS}-${ARCH}"

TMPDIR_DL="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_DL"' EXIT

info "chirp 一键安装"
info "  系统: ${OS_RAW} (${TARGET})"
info "  镜像: ${MIRROR}"

# ---------- manifest（可用面清单，架构不认识在这里报清楚） ----------
MANIFEST="$TMPDIR_DL/manifest.json"
if ! fetch "$MIRROR/manifest.json" "$MANIFEST"; then
	die "拉取 manifest 失败: $MIRROR/manifest.json
  - 404/无产物：nightly-dist 分支可能尚未生成——每日构建在近 24h 有提交时于 19:23 UTC 重建（也可在 Actions 手动 dispatch nightly.yml）；
  - 网络问题：请检查代理或用 --mirror 指向自建镜像。"
fi

manifest_meta() {
	# 提取单行 JSON 里的标量字段: manifest_meta <key>
	sed "s/.*\"$1\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p;d" "$MANIFEST"
}

targets_of() {
	# targets_of <组件>:打印该组件的架构清单（逗号串）
	sed "s/.*\"$1\"[[:space:]]*:[[:space:]]*\[\([^]]*\)\].*/\1/p;d" "$MANIFEST" |
		tr -d ' \t"'
}

have() {
	# have <组件>:本机 TARGET 可安装时返回 0
	local list
	list="$(targets_of "$1")"
	[ -n "$list" ] || return 1
	case ",$list," in
	*,any,*|*",${TARGET},"*) return 0 ;;
	*) return 1 ;;
	esac
}

info "  构建: stamp=$(manifest_meta stamp) commit=$(manifest_meta commit | cut -c1-7)"
info ""

need_component() {
	# need_component <组件>:不可用即明确报错（指明矩阵缺口）
	if ! have "$1"; then
		if [ "$1" = "desktop" ] || [ "$1" = "cpp" ]; then
			die "组件 $1 在 ${TARGET} 无每日产物——矩阵当前覆盖 Linux x86_64/aarch64、macOS arm64、Windows x64（见 nightly.yml 头注）；go/ts 组件与架构无关，可先装"
		fi
		die "组件 $1 在镜像 manifest 中不存在: $MIRROR"
	fi
}

# ---------- 组件安装 ----------
install_desktop_app() {
	need_component desktop

	# ---- macOS:dmg 挂载拷贝 .app ----------------------------------------
	if [ "$OS" = "darwin" ]; then
		local dmg="$TMPDIR_DL/chirp-desktop.dmg"
		fetch "$MIRROR/desktop/$TARGET/chirp-desktop-$TARGET.dmg" "$dmg" ||
			die "下载失败: $MIRROR/desktop/$TARGET/chirp-desktop-$TARGET.dmg"

		local mnt="$TMPDIR_DL/dmg-mnt" appsrc dest
		mkdir -p "$mnt"
		hdiutil attach -nobrowse -readonly -mountpoint "$mnt" "$dmg" >/dev/null ||
			die "hdiutil attach 失败（下载不完整？）: $dmg"
		appsrc="$(find "$mnt" -maxdepth 2 -name '*.app' -type d -print -quit)"
		if [ -z "$appsrc" ]; then
			hdiutil detach "$mnt" >/dev/null 2>&1 || true
			die "dmg 内未找到 .app（产物异常？）"
		fi

		# /Applications 可写（admin 组）或免密 sudo → 系统位;否则用户位
		dest="/Applications"
		if [ ! -w /Applications ] && ! as_root true 2>/dev/null; then
			dest="${HOME}/Applications"
			mkdir -p "$dest"
		fi
		info "安装 .app（$(basename "$appsrc")）到 $dest ..."
		rm -rf "$dest/Chirp.app"
		if [ "$dest" = "/Applications" ] && [ ! -w "$dest" ]; then
			as_root cp -R "$appsrc" "$dest/" || { hdiutil detach "$mnt" >/dev/null 2>&1 || true
				die "复制 .app 失败: $dest"; }
		else
			cp -R "$appsrc" "$dest/" || { hdiutil detach "$mnt" >/dev/null 2>&1 || true
				die "复制 .app 失败: $dest"; }
		fi
		hdiutil detach "$mnt" >/dev/null 2>&1 || true

		# 未签名未公证:清隔离属性免「无法验证开发者」首启拦截(best-effort;
		# 拒绝执行也不影响落位,右键打开是官方兜底路径)
		xattr -dr com.apple.quarantine "$dest/Chirp.app" 2>/dev/null || true
		[ -d "$dest/Chirp.app" ] || die "安装后验证失败: 未找到 $dest/Chirp.app"
		info "已安装: Chirp -> $dest/Chirp.app"
		warn "产物未签名未公证——若首启仍被 Gatekeeper 拦截:右键 App 选「打开」,或到系统设置允许"
		return
	fi

	# ---- Linux:.deb（dpkg 或用户态解包）----------------------------------
	command -v dpkg >/dev/null 2>&1 ||
		die "app 组件是 .deb 包，需要 dpkg（Debian/Ubuntu 系）——其他发行版请用 --component cpp 安装 C++ SDK，或到 nightly-dist 手动取包转换"

	local deb="$TMPDIR_DL/chirp-desktop.deb"
	fetch "$MIRROR/desktop/$TARGET/chirp-desktop-$TARGET.deb" "$deb" ||
		die "下载失败: $MIRROR/desktop/$TARGET/chirp-desktop-$TARGET.deb"

	local version pkg
	version="$(dpkg-deb -f "$deb" Version)" ||
		die "读取包元数据失败（下载不完整？）: $deb"
	pkg="$(dpkg-deb -f "$deb" Package)"

	if [ "$(id -u)" -eq 0 ] || as_root true 2>/dev/null; then
		info "安装 .deb（dpkg -i，Chirp $version）..."
		as_root dpkg -i "$deb" ||
			die "dpkg -i 失败——多为缺依赖，请执行: sudo apt-get install -f 后重跑本脚本"
		# 装完验证:包在 dpkg 库里且带可执行文件
		dpkg -s "$pkg" >/dev/null 2>&1 || die "安装后验证失败: dpkg 库中找不到 $pkg"
		dpkg -L "$pkg" | grep -q '/bin/' ||
			warn "包内未发现 bin/ 可执行文件——请核对包内容: dpkg -L $pkg"
		info "已安装: Chirp $version（dpkg 包 $pkg，启动器已进系统菜单/PATH）"
	else
		# 无 root:解包进用户目录,启动器链到 ~/.local/bin（放 PATH）
		local appdir="${HOME}/.local/share/chirp/app"
		local bindir="${HOME}/.local/bin"
		info "无 root 权限:解包进 $appdir（Chirp $version）..."
		rm -rf "$appdir"
		mkdir -p "$appdir" "$bindir"
		dpkg -x "$deb" "$appdir" || die "dpkg -x 解包失败"
		local found=0 bin
		for bin in "$appdir"/usr/bin/*; do
			[ -f "$bin" ] || continue
			found=1
			chmod 0755 "$bin" 2>/dev/null || true
			ln -sfn "$bin" "$bindir/$(basename "$bin")"
			info "  启动器: $bindir/$(basename "$bin") -> $bin"
		done
		[ "$found" -eq 1 ] || die "解包异常：包内未找到 usr/bin 可执行文件"
		case ":$PATH:" in
		*":$bindir:"*) ;;
		*) warn "$bindir 不在当前 PATH 中——请加入 PATH 后再直接启动" ;;
		esac
		warn "解包形态不含系统菜单项与依赖检查——运行缺库时请安装 libwebkit2gtk-4.1-0、libgtk-3-0"
		info "已安装: Chirp $version（用户态解包，重跑本脚本即升级）"
	fi
}

install_cpp_sdk() {
	need_component cpp
	local tgz="$TMPDIR_DL/chirp-cpp-sdk.tar.gz"
	fetch "$MIRROR/cpp/chirp-cpp-sdk-$TARGET.tar.gz" "$tgz" ||
		die "下载失败: $MIRROR/cpp/chirp-cpp-sdk-$TARGET.tar.gz"

	# 前缀决策:root/免密 sudo → /usr/local；否则 ~/.local
	if [ -z "$PREFIX" ]; then
		if [ "$(id -u)" -eq 0 ] || as_root true 2>/dev/null; then
			PREFIX="/usr/local"
		else
			PREFIX="${HOME}/.local"
		fi
	fi
	info "安装 C++ core SDK 到 $PREFIX ..."

	tar xzf "$tgz" -C "$TMPDIR_DL"
	local src="$TMPDIR_DL/chirp-cpp-sdk"
	[ -d "$src/lib" ] && [ -d "$src/include" ] ||
		die "解包异常：tarball 内未找到 chirp-cpp-sdk/{lib,include}"

	local incdir="$PREFIX/include" libdir="$PREFIX/lib" datadir="$PREFIX/share/chirp-cpp-sdk"
	# 可写探测只能用 [ -w ]:mkdir -p 对已存在目录恒返回 0(不检查写权限),
	# 拿它的返回值当判据会把 /usr/local 这类只读前缀误判为可写,然后在
	# 用户分支里裸撞 Permission denied(真机走查抓到的回归)。
	if [ ! -d "$PREFIX" ]; then
		mkdir -p "$PREFIX" 2>/dev/null || true
	fi
	if [ -w "$PREFIX" ]; then
		mkdir -p "$incdir" "$libdir" "$datadir/proto"
		cp -R "$src/include/." "$incdir/"
		cp -R "$src/lib/." "$libdir/"
		cp -R "$src/proto/." "$datadir/proto/"
		[ -f "$src/NOTE.txt" ] && cp "$src/NOTE.txt" "$datadir/NOTE.txt"
	else
		as_root mkdir -p "$incdir" "$libdir" "$datadir/proto" ||
			die "无法创建前缀目录: $PREFIX（可用 --prefix 指定可写目录）"
		as_root cp -R "$src/include/." "$incdir/"
		as_root cp -R "$src/lib/." "$libdir/"
		as_root cp -R "$src/proto/." "$datadir/proto/"
		[ -f "$src/NOTE.txt" ] && as_root cp "$src/NOTE.txt" "$datadir/NOTE.txt"
	fi

	# 动态库加载路径:系统级前缀 → ldconfig 刷新(Linux);否则给导出提示
	if [ "$PREFIX" = "/usr/local" ] && command -v ldconfig >/dev/null 2>&1; then
		as_root ldconfig 2>/dev/null || true
	elif [ "$PREFIX" != "/usr/local" ]; then
		if [ "$OS" = "darwin" ]; then
			warn "链接/运行请加: -I$incdir -L$libdir,运行导出 DYLD_LIBRARY_PATH=$libdir"
		else
			warn "链接/运行请加: -I$incdir -L$libdir,并导出 LD_LIBRARY_PATH=$libdir"
		fi
	fi

	# 装完验证:nm 符号探针(NOTE.txt 记载的标准验证命令)。macOS 的 nm 不
	# 接 -C(c++ 解名是 llvm-nm 的活),直接 grep mangle 前缀 _ZN5chirp。
	# 落文件再 grep:pipefail 下 `nm | grep -m1` 是竞态——grep 早退后
	# nm 写几百行符号会吃 SIGPIPE(141),管道整体判失败(真机走查抓到)。
	local static_lib="$libdir/libchirp_core_sdk_static.a"
	[ -f "$static_lib" ] || die "安装后验证失败:未找到 $static_lib"
	if command -v nm >/dev/null 2>&1; then
		local syms="$TMPDIR_DL/nm.out" pattern="chirp::sdk"
		if [ "$OS" = "darwin" ]; then
			pattern="_ZN5chirp"
		fi
		nm -C "$static_lib" > "$syms" 2>/dev/null ||
			nm "$static_lib" > "$syms" 2>/dev/null || true
		grep -m1 "$pattern" "$syms" >/dev/null ||
			die "安装后验证失败:$static_lib 中未发现 $pattern 符号（产物损坏？）"
	fi
	info "已安装: C++ core SDK ($TARGET) -> $libdir 与 $incdir"
	CPP_LIBDIR="$libdir"
}

sdk_dir_for() {
	# sdk_dir_for <组件>:源码包落位目录
	printf '%s\n' "${INSTALL_DIR:-${HOME}/.local/share/chirp/$1}"
}

install_go_sdk() {
	need_component go
	local dir
	dir="$(sdk_dir_for go)"
	local tgz="$TMPDIR_DL/chirp-go-sdk.tar.gz"
	fetch "$MIRROR/go/chirp-go-sdk.tar.gz" "$tgz" ||
		die "下载失败: $MIRROR/go/chirp-go-sdk.tar.gz"
	info "安装 Go SDK 源码包到 $dir ..."
	rm -rf "$dir"
	mkdir -p "$dir"
	tar xzf "$tgz" -C "$dir" || die "解包失败: chirp-go-sdk.tar.gz"
	[ -d "$dir/sdks/go" ] || die "解包异常：未找到 sdks/go/"
	if command -v go >/dev/null 2>&1; then
		# 验证需要网络拉 protobuf 依赖:失败降级为警告(不影响源码包本体)
		if (cd "$dir" && go build ./sdks/go/... 2>/dev/null); then
			info "已安装并验证: Go SDK -> $dir（go build ./sdks/go/... 通过）"
		else
			warn "已安装到 $dir,但 go build 验证未过——多为此机离线拉不到依赖,联网后可自行复验"
		fi
	else
		info "已安装: Go SDK -> $dir（本机无 go 工具链,跳过构建验证）"
	fi
	info "  接入: 把 $dir 内容放到你的 module 根（或并入后改 module 名），"
	info "        import \"github.com/cui/chirp/sdks/go\""
}

install_ts_pkg() {
	need_component ts
	local dir
	dir="$(sdk_dir_for ts)"
	local tgz="$TMPDIR_DL/chirp-protocol.tgz"
	fetch "$MIRROR/ts/chirp-protocol.tgz" "$tgz" ||
		die "下载失败: $MIRROR/ts/chirp-protocol.tgz"
	info "安装 @chirp/protocol npm 包到 $dir ..."
	mkdir -p "$dir"
	cp -f "$tgz" "$dir/chirp-protocol.tgz"
	# 同上:列表落文件再 grep(早退 grep 在 pipefail 下有 SIGPIPE 竞态)
	tar -tzf "$dir/chirp-protocol.tgz" > "$TMPDIR_DL/tgz.list" 2>/dev/null ||
		die "安装后验证失败:tgz 解包列表读取失败（产物损坏？）"
	grep -q 'package/src/' "$TMPDIR_DL/tgz.list" ||
		die "安装后验证失败:tgz 内未找到 package/src/（产物损坏？）"
	local pkg_version
	pkg_version="$(tar -xzOf "$dir/chirp-protocol.tgz" package/package.json 2>/dev/null |
		sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -1 || true)"
	info "已安装: @chirp/protocol ${pkg_version:-?} -> $dir/chirp-protocol.tgz"
	info "  接入: 在项目里执行 npm install $dir/chirp-protocol.tgz"
}

# ---------- 执行 ----------
case "$COMPONENT" in
app) install_desktop_app ;;
cpp) install_cpp_sdk ;;
go) install_go_sdk ;;
ts) install_ts_pkg ;;
all)
	# all:逐组件装,桌面 App 缺平台时降级为警告(其余照装)
	if have desktop && { [ "$OS" = "darwin" ] ||
		{ [ "$OS" = "linux" ] && command -v dpkg >/dev/null 2>&1; }; }; then
		install_desktop_app
	else
		warn "跳过 app 组件:${TARGET} 无可用桌面产物（或无 dpkg）"
	fi
	install_cpp_sdk
	install_go_sdk
	install_ts_pkg
	;;
esac

info ""
info "完成。重跑本脚本即升级（幂等）。"
case "$COMPONENT" in
app) info "  验证: Linux 见 dpkg 包元数据 / macOS ls /Applications/Chirp.app;启动 Chirp 即可" ;;
cpp) info "  验证: nm -C ${CPP_LIBDIR:-\$PREFIX/lib}/libchirp_core_sdk_static.a | grep -m3 'chirp::sdk'" ;;
go | ts) info "  验证: 见上方接入方式" ;;
esac
