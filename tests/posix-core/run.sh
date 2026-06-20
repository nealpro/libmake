#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [ "${LMK_POSIX_TMP_ROOT:-}" ]; then
tmp_root=$LMK_POSIX_TMP_ROOT
rm -rf "$tmp_root"
mkdir -p "$tmp_root"
tmp_root=$(CDPATH= cd -- "$tmp_root" && pwd)
else
tmp_root=$(mktemp -d "${TMPDIR:-/tmp}/libmake-posix.XXXXXX")
fi
runner="$tmp_root/lmk_runner"
libmake="$tmp_root/libmake"
obj_dir="$tmp_root/obj"
cc=${CC:-cc}
cflags=${CFLAGS:-}
ldflags=${LDFLAGS:-}
make_cmd=${MAKE:-make}

cleanup()
{
if [ "${LMK_POSIX_KEEP_TMP:-}" = "1" ]; then
return
fi
rm -rf "$tmp_root"
}
trap cleanup EXIT INT TERM HUP

get_mtime()
{
if stat -f %m "$1" >/dev/null 2>&1; then
stat -f %m "$1"
else
stat -c %Y "$1"
fi
}

run_make_status()
{
dir=$1
target=${2:-}
set +e
(
cd "$dir"
if [ "$target" ]; then
"$make_cmd" -f Makefile "$target" >/dev/null 2>&1
else
"$make_cmd" -f Makefile >/dev/null 2>&1
fi
)
status=$?
set -e
echo "$status"
}

run_lmk_status()
{
dir=$1
target=${2:-}
set +e
(
cd "$dir"
if [ "$target" ]; then
"$runner" Makefile "$target" >/dev/null 2>&1
else
"$runner" Makefile >/dev/null 2>&1
fi
)
status=$?
set -e
echo "$status"
}

run_lmk_cli_status()
{
dir=$1
target=${2:-}
set +e
(
cd "$dir"
if [ "$target" ]; then
"$libmake" -f Makefile "$target" >/dev/null 2>&1
else
"$libmake" -f Makefile >/dev/null 2>&1
fi
)
status=$?
set -e
echo "$status"
}

fail()
{
echo "FAIL: $*" >&2
exit 1
}

assert_eq()
{
left=$1
right=$2
msg=$3
[ "$left" = "$right" ] || fail "$msg (left=$left right=$right)"
}

copy_fixture()
{
src=$1
dst=$2
cp -R "$src" "$dst"
}

echo "[1/5] building libmake differential runners"
mkdir -p "$obj_dir"
"$cc" $cflags -I"$repo_dir/src" -c \
"$repo_dir/tests/posix-core/lmk_runner.c" -o "$obj_dir/lmk_runner.o"
"$cc" $cflags -I"$repo_dir/src" -c \
"$repo_dir/src/main.c" -o "$obj_dir/main.o"
"$cc" $cflags -I"$repo_dir/src" -c \
"$repo_dir/src/libmake.c" -o "$obj_dir/libmake.o"
"$cc" $cflags -I"$repo_dir/src" -c \
"$repo_dir/src/dag.c" -o "$obj_dir/dag.o"
"$cc" $cflags -I"$repo_dir/src" -c \
"$repo_dir/src/exec.c" -o "$obj_dir/exec.o"
"$cc" $ldflags -o "$runner" \
"$obj_dir/lmk_runner.o" \
"$obj_dir/libmake.o" \
"$obj_dir/dag.o" \
"$obj_dir/exec.o"
"$cc" $ldflags -o "$libmake" \
"$obj_dir/main.o" \
"$obj_dir/libmake.o" \
"$obj_dir/dag.o" \
"$obj_dir/exec.o"

echo "[2/5] basic build + up-to-date + stale-rebuild"
base_basic="$tmp_root/base-basic"
mkdir -p "$base_basic"
cat > "$base_basic/Makefile" <<'MK'
all: out.txt

out.txt: in.txt ; cp in.txt out.txt
MK
echo "v1" > "$base_basic/in.txt"

make_dir="$tmp_root/make-basic"
lmk_dir="$tmp_root/lmk-basic"
copy_fixture "$base_basic" "$make_dir"
copy_fixture "$base_basic" "$lmk_dir"

assert_eq "$(run_make_status "$make_dir" all)" "0" "make basic initial build"
assert_eq "$(run_lmk_status "$lmk_dir" all)" "0" "libmake basic initial build"
assert_eq "$(run_lmk_cli_status "$lmk_dir" all)" "0" "libmake CLI -f target"

[ -f "$make_dir/out.txt" ] || fail "make did not produce out.txt"
[ -f "$lmk_dir/out.txt" ] || fail "libmake did not produce out.txt"
assert_eq "$(cat "$make_dir/out.txt")" "$(cat "$lmk_dir/out.txt")" \
"basic build output mismatch"

make_before=$(get_mtime "$make_dir/out.txt")
lmk_before=$(get_mtime "$lmk_dir/out.txt")
assert_eq "$(run_make_status "$make_dir" all)" "0" "make up-to-date run"
assert_eq "$(run_lmk_status "$lmk_dir" all)" "0" "libmake up-to-date run"
make_after=$(get_mtime "$make_dir/out.txt")
lmk_after=$(get_mtime "$lmk_dir/out.txt")
assert_eq "$make_before" "$make_after" "make rebuilt unexpectedly"
assert_eq "$lmk_before" "$lmk_after" "libmake rebuilt unexpectedly"

sleep 1
echo "v2" > "$make_dir/in.txt"
echo "v2" > "$lmk_dir/in.txt"
assert_eq "$(run_make_status "$make_dir" all)" "0" "make stale rebuild"
assert_eq "$(run_lmk_status "$lmk_dir" all)" "0" "libmake stale rebuild"
make_new=$(get_mtime "$make_dir/out.txt")
lmk_new=$(get_mtime "$lmk_dir/out.txt")
[ "$make_new" -gt "$make_after" ] || fail "make did not rebuild stale target"
[ "$lmk_new" -gt "$lmk_after" ] || fail "libmake did not rebuild stale target"
assert_eq "$(cat "$make_dir/out.txt")" "$(cat "$lmk_dir/out.txt")" \
"stale rebuild output mismatch"

echo "[3/5] parser rules: comments, repeated deps, tab and semicolon recipes"
base_parser="$tmp_root/base-parser"
mkdir -p "$base_parser"
cat > "$base_parser/Makefile" <<'MK'
# First non-special target is the default.
all: first second

first: input.txt
first: extra.txt
	cat input.txt extra.txt > first

extra.txt: ; printf 'extra\n' > extra.txt
second: first ; cp first second
MK
echo "input" > "$base_parser/input.txt"

make_parser="$tmp_root/make-parser"
lmk_parser="$tmp_root/lmk-parser"
copy_fixture "$base_parser" "$make_parser"
copy_fixture "$base_parser" "$lmk_parser"

assert_eq "$(run_make_status "$make_parser")" "0" "make default target"
assert_eq "$(run_lmk_status "$lmk_parser")" "0" "libmake default target"
assert_eq "$(run_lmk_cli_status "$lmk_parser")" "0" "libmake CLI default target"
assert_eq "$(cat "$make_parser/first")" "$(cat "$lmk_parser/first")" \
"default target first output mismatch"
assert_eq "$(cat "$make_parser/second")" "$(cat "$lmk_parser/second")" \
"default target second output mismatch"

echo "[4/5] missing-target error behavior"
base_missing="$tmp_root/base-missing"
mkdir -p "$base_missing"
cat > "$base_missing/Makefile" <<'MK'
all: ; @:
MK
make_missing="$tmp_root/make-missing"
lmk_missing="$tmp_root/lmk-missing"
copy_fixture "$base_missing" "$make_missing"
copy_fixture "$base_missing" "$lmk_missing"

make_status=$(run_make_status "$make_missing" missing)
lmk_status=$(run_lmk_status "$lmk_missing" missing)
[ "$make_status" -ne 0 ] || fail "make missing target should fail"
[ "$lmk_status" -ne 0 ] || fail "libmake missing target should fail"

echo "[5/5] recipe failure behavior"
base_fail="$tmp_root/base-fail"
mkdir -p "$base_fail"
cat > "$base_fail/Makefile" <<'MK'
all: bad

bad: ; sh -c 'exit 7'
MK
make_fail="$tmp_root/make-fail"
lmk_fail="$tmp_root/lmk-fail"
copy_fixture "$base_fail" "$make_fail"
copy_fixture "$base_fail" "$lmk_fail"

make_status=$(run_make_status "$make_fail" all)
lmk_status=$(run_lmk_status "$lmk_fail" all)
[ "$make_status" -ne 0 ] || fail "make failing recipe should fail"
[ "$lmk_status" -ne 0 ] || fail "libmake failing recipe should fail"

echo "PASS: core differential checks succeeded"
