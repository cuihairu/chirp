#!/usr/bin/env bash
# 分离符号并生成 breakpad 格式 .sym(docs/design-notes/CRASH_COLLECTION.md)。
# 用法: extract_symbols.sh <binary> [symbols_dir]   (缺省 symbols_dir=./symbols)
#
# 产物:
#   <symbols_dir>/<name>.debug       objcopy 分离的完整调试符号(归档)
#   <symbols_dir>/<name>/<id>/<name>.sym  dump_syms 生成的 breakpad 符号,
#                                     已按 minidump-stackwalk 的符号服务器布局
#                                     摆放(--symbols 直接指 <symbols_dir>)。
# 原二进制不动;发布面需要瘦身时另跑 strip --strip-debug。
#
# 依赖: binutils(objcopy) + rust-minidump 套件(cargo install dump_syms)。
set -euo pipefail

if [ $# -lt 1 ]; then
  echo "usage: $0 <binary> [symbols_dir]" >&2
  exit 1
fi

bin=$1
sym_dir=${2:-symbols}
name=$(basename "$bin")

mkdir -p "$sym_dir"
objcopy --only-keep-debug "$bin" "$sym_dir/$name.debug"

dump_syms "$bin" > "$sym_dir/$name.sym"

# MODULE 行形态: MODULE <os> <arch> <debug-id> <name>;id 在第 4 列。
id=$(awk 'NR==1 && $1=="MODULE" {print $4; exit}' "$sym_dir/$name.sym")
if [ -z "$id" ]; then
  echo "error: dump_syms output has no MODULE id line" >&2
  exit 1
fi

layout="$sym_dir/$name/$id"
mkdir -p "$layout"
mv "$sym_dir/$name.sym" "$layout/$name.sym"

echo "symbols: $layout/$name.sym (debug-id $id)"
