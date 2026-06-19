#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [ "${LMK_API_TMP_ROOT:-}" ]; then
tmp_root=$LMK_API_TMP_ROOT
rm -rf "$tmp_root"
mkdir -p "$tmp_root"
tmp_root=$(CDPATH= cd -- "$tmp_root" && pwd)
else
tmp_root=$(mktemp -d "${TMPDIR:-/tmp}/libmake-api.XXXXXX")
fi
runner="$tmp_root/api_tests"
obj_dir="$tmp_root/obj"
work_dir="$tmp_root/work"
cc=${CC:-cc}
cflags=${CFLAGS:-}
ldflags=${LDFLAGS:-}

cleanup()
{
if [ "${LMK_API_KEEP_TMP:-}" = "1" ]; then
return
fi
rm -rf "$tmp_root"
}
trap cleanup EXIT INT TERM HUP

echo "[1/2] building api_tests"
mkdir -p "$obj_dir" "$work_dir"
"$cc" $cflags -I"$repo_dir/src" -c \
"$repo_dir/tests/api/api_tests.c" -o "$obj_dir/api_tests.o"
"$cc" $cflags -I"$repo_dir/src" -c \
"$repo_dir/src/libmake.c" -o "$obj_dir/libmake.o"
"$cc" $cflags -I"$repo_dir/src" -c \
"$repo_dir/src/dag.c" -o "$obj_dir/dag.o"
"$cc" $cflags -I"$repo_dir/src" -c \
"$repo_dir/src/exec.c" -o "$obj_dir/exec.o"
"$cc" $ldflags -o "$runner" \
"$obj_dir/api_tests.o" \
"$obj_dir/libmake.o" \
"$obj_dir/dag.o" \
"$obj_dir/exec.o"

echo "[2/2] running API checks"
"$runner" "$work_dir"
