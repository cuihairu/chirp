# chirp 一键安装（Windows PowerShell 5.1+ / pwsh）
#
# 从每日构建镜像分支（nightly-dist，匿名可直链下载）拉取与本机架构匹配的
# 产物并安装。组件: app / cpp / go / ts / all（默认 app）。
#
# 用法:
#   # 一键（自动检测架构）:
#   irm https://raw.githubusercontent.com/cuihairu/chirp/main/install.ps1 | iex
#
#   # 带参数（管道形态传参不便时，用 scriptblock Create）:
#   & ([scriptblock]::Create((irm https://raw.githubusercontent.com/cuihairu/chirp/main/install.ps1))) `
#       -Component cpp -Prefix "$env:LOCALAPPDATA\chirp"
#
#   # 管道形态下用环境变量:
#   $env:CHIRP_COMPONENT='cpp'; irm <url> | iex
#
# 说明:
#   - 幂等：重跑即升级（覆盖产物）。
#   - 失败即停：任何一步失败明确报错退出。
#   - 平台面由每日构建矩阵决定（见 nightly.yml）：当前 Linux x86_64/aarch64；
#     Windows 构建腿待补——届时 app/cpp 组件在此放开，go/ts 现在即可装。
#   - 无常驻服务可注册（服务端不在每日构建内），故无 -WithService 选项。
#requires -Version 5.1

param(
    [string]$Component = '',
    [string]$InstallDir = '',
    [string]$Prefix = '',
    [string]$Mirror = ''
)

$ErrorActionPreference = 'Stop'

# 管道形态（irm | iex）传不了参数:环境变量兜底
if (-not $Component) { $Component = if ($env:CHIRP_COMPONENT) { $env:CHIRP_COMPONENT } else { 'app' } }
if (@('app', 'cpp', 'go', 'ts', 'all') -notcontains $Component) {
    Stop-WithError "未知组件: $Component —— 可选 app / cpp / go / ts / all"
}
if (-not $InstallDir) { $InstallDir = $env:CHIRP_INSTALL_DIR }
if (-not $Prefix) { $Prefix = $env:CHIRP_PREFIX }
if (-not $Mirror) {
    $Mirror = if ($env:CHIRP_MIRROR) { $env:CHIRP_MIRROR }
    else { 'https://raw.githubusercontent.com/cuihairu/chirp/refs/heads/nightly-dist' }
}

function Write-Info([string]$Message) { Write-Host $Message }
function Stop-WithError([string]$Message) { throw "错误: $Message" }

# ---------- 纯逻辑:架构与可用面（便于脱离 Windows 环境校验） ----------
function Get-TargetArch {
    # 32 位 PowerShell 跑在 64 位系统上时 PROCESSOR_ARCHITECTURE 是 x86,
    # 真实架构在 PROCESSOR_ARCHITEW6432
    $pa = $env:PROCESSOR_ARCHITECTURE
    if ($env:PROCESSOR_ARCHITEW6432) { $pa = $env:PROCESSOR_ARCHITEW6432 }
    switch ($pa) {
        'AMD64' { return 'windows-x64' }
        'ARM64' { return 'windows-arm64' }
        'x86' { Stop-WithError "不支持的架构: 32 位 x86 —— 每日构建未提供 32 位产物" }
        default { Stop-WithError "不支持的架构: $pa —— 已支持: AMD64(x86_64)、ARM64;矩阵面见 nightly.yml 与 nightly-dist/manifest.json" }
    }
}

function Get-Manifest {
    $url = "$Mirror/manifest.json"
    try {
        $raw = (Invoke-WebRequest -Uri $url -UseBasicParsing).Content
        return $raw | ConvertFrom-Json
    } catch {
        Stop-WithError ("拉取 manifest 失败: $url`n" +
            "  - 404/无产物：nightly-dist 分支可能尚未生成——每日构建在近 24h 有提交时于 19:23 UTC 重建（也可在 Actions 手动 dispatch nightly.yml）`n" +
            "  - 网络问题：请检查代理或用 -Mirror / `$env:CHIRP_MIRROR 指向自建镜像`n" +
            "  - 原始错误: $($_.Exception.Message)")
    }
}

function Test-ComponentAvailable($Manifest, [string]$ComponentName, [string]$Target) {
    $prop = $Manifest.components.PSObject.Properties[$ComponentName]
    if (-not $prop) { return $false }
    $targets = @($prop.Value)
    return ($targets -contains 'any') -or ($targets -contains $Target)
}

function Assert-ComponentAvailable($Manifest, [string]$ComponentName, [string]$Target) {
    if (Test-ComponentAvailable $Manifest $ComponentName $Target) { return }
    if ($ComponentName -eq 'app' -or $ComponentName -eq 'cpp') {
        Stop-WithError ("组件 $ComponentName 在 $Target 无每日产物——矩阵当前只覆盖 Linux x86_64/aarch64" +
            "（windows/darwin 构建腿待补，见 nightly.yml 头注）；go/ts 组件与架构无关，可先装")
    }
    Stop-WithError "组件 $ComponentName 在镜像 manifest 中不存在: $Mirror"
}

function Get-DownloadFile {
    param([string]$Url, [string]$OutFile)
    try {
        Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing
    } catch {
        Stop-WithError "下载失败: $Url`n  原始错误: $($_.Exception.Message)"
    }
    if (-not (Test-Path $OutFile)) { Stop-WithError "下载未产出文件: $OutFile" }
}

function Expand-Tarball {
    param([string]$Tarball, [string]$Destination)
    if (-not (Get-Command tar -ErrorAction SilentlyContinue)) {
        Stop-WithError "解包需要 tar（Windows 10 1803+ 自带）——本机未找到 tar.exe"
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & tar -xzf $Tarball -C $Destination
    if ($LASTEXITCODE -ne 0) { Stop-WithError "解包失败: $Tarball（tar 退出码 $LASTEXITCODE）" }
}

# ---------- 组件安装 ----------
function Install-GoSdk {
    $dir = if ($InstallDir) { $InstallDir } else { Join-Path $env:LOCALAPPDATA 'chirp\go-sdk' }
    $tgz = Join-Path ([System.IO.Path]::GetTempPath()) 'chirp-go-sdk.tar.gz'
    Write-Info "安装 Go SDK 源码包到 $dir ..."
    Get-DownloadFile "$Mirror/go/chirp-go-sdk.tar.gz" $tgz
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    Expand-Tarball $tgz $dir
    if (-not (Test-Path (Join-Path $dir 'sdks\go'))) { Stop-WithError "解包异常：未找到 sdks\go/" }
    if (Get-Command go -ErrorAction SilentlyContinue) {
        Push-Location $dir
        $ok = $false
        try { go build ./sdks/go/... 2>$null; $ok = ($LASTEXITCODE -eq 0) } catch { $ok = $false }
        Pop-Location
        if ($ok) {
            Write-Info "已安装并验证: Go SDK -> $dir（go build ./sdks/go/... 通过）"
        } else {
            Write-Warning "已安装到 $dir,但 go build 验证未过——多为此机离线拉不到依赖,联网后可自行复验"
        }
    } else {
        Write-Info "已安装: Go SDK -> $dir（本机无 go 工具链,跳过构建验证）"
    }
    Write-Info "  接入: 把 $dir 内容放到你的 module 根（或并入后改 module 名），"
    Write-Info '        import "github.com/cui/chirp/sdks/go"'
}

function Install-TsPackage {
    $dir = if ($InstallDir) { $InstallDir } else { Join-Path $env:LOCALAPPDATA 'chirp\ts' }
    $tgz = Join-Path $dir 'chirp-protocol.tgz'
    Write-Info "安装 @chirp/protocol npm 包到 $dir ..."
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Get-DownloadFile "$Mirror/ts/chirp-protocol.tgz" $tgz
    # 验证:包体可列出且含 src/
    $listing = & tar -tzf $tgz
    if ($LASTEXITCODE -ne 0 -or -not ($listing -match 'package/src/')) {
        Stop-WithError "安装后验证失败:tgz 内未找到 package/src/（产物损坏？）"
    }
    Write-Info "已安装: @chirp/protocol -> $tgz"
    Write-Info "  接入: 在项目里执行 npm install `"$tgz`""
}

# ---------- 执行 ----------
Write-Info 'chirp 一键安装'
$Target = Get-TargetArch
Write-Info "  系统: Windows ($Target)"
Write-Info "  镜像: $Mirror"

$Manifest = Get-Manifest
Write-Info "  构建: stamp=$($Manifest.stamp) commit=$($Manifest.commit.Substring(0, [Math]::Min(7, $Manifest.commit.Length)))"
Write-Info ''

switch ($Component) {
    'go' { Assert-ComponentAvailable $Manifest 'go' $Target; Install-GoSdk }
    'ts' { Assert-ComponentAvailable $Manifest 'ts' $Target; Install-TsPackage }
    'app' {
        # 可用面检查先行(windows 腿落地前在此明确报错);腿落地后补下载+安装
        Assert-ComponentAvailable $Manifest 'desktop' $Target
        Stop-WithError "desktop 组件的 Windows 安装路径尚未实现——windows 构建腿待补,见 nightly.yml 头注"
    }
    'cpp' {
        Assert-ComponentAvailable $Manifest 'cpp' $Target
        Stop-WithError "cpp 组件的 Windows 安装路径尚未实现——windows 构建腿待补,见 nightly.yml 头注"
    }
    'all' {
        if (Test-ComponentAvailable $Manifest 'desktop' $Target) {
            Write-Warning "app 组件:Windows 安装路径尚未实现,跳过(见 nightly.yml 头注)"
        } else {
            Write-Warning "跳过 app 组件:$Target 无每日产物(构建腿待补)"
        }
        if (Test-ComponentAvailable $Manifest 'cpp' $Target) {
            Write-Warning "cpp 组件:Windows 安装路径尚未实现,跳过(见 nightly.yml 头注)"
        } else {
            Write-Warning "跳过 cpp 组件:$Target 无每日产物(构建腿待补)"
        }
        Assert-ComponentAvailable $Manifest 'go' $Target
        Install-GoSdk
        Assert-ComponentAvailable $Manifest 'ts' $Target
        Install-TsPackage
    }
}

Write-Info ''
Write-Info '完成。重跑本脚本即升级（幂等）。'
