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
obj_dir="$tmp_root/obj"
cc=${CC:-cc}
cflags=${CFLAGS:-}
ldflags=${LDFLAGS:-}

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
	target=$2
	set +e
	(
		cd "$dir"
		make "$target" >/dev/null 2>&1
	)
status=$?
set -e
echo "$status"
}

run_lmk_status()
{
dir=$1
scenario=$2
target=$3
set +e
(
cd "$dir"
"$runner" "$scenario" "$target" >/dev/null 2>&1
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

echo "[1/4] building lmk_runner"
mkdir -p "$obj_dir"
"$cc" $cflags -I"$repo_dir/src" -c \
"$repo_dir/tests/posix-core/lmk_runner.c" -o "$obj_dir/lmk_runner.o"
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

echo "[2/4] basic build + up-to-date + stale-rebuild"
base_basic="$tmp_root/base-basic"
mkdir -p "$base_basic"
cat > "$base_basic/Makefile" <<'MK'
all: out.txt

out.txt: in.txt ; cp in.txt out.txt
MK
echo "v1" > "$base_basic/in.txt"

make_dir="$tmp_root/make-basic"
lmk_dir="$tmp_root/lmk-basic"
cp -R "$base_basic" "$make_dir"
cp -R "$base_basic" "$lmk_dir"

assert_eq "$(run_make_status "$make_dir" all)" "0" "make basic initial build"
assert_eq "$(run_lmk_status "$lmk_dir" basic all)" "0" "libmake basic initial build"

[ -f "$make_dir/out.txt" ] || fail "make did not produce out.txt"
[ -f "$lmk_dir/out.txt" ] || fail "libmake did not produce out.txt"

make_before=$(get_mtime "$make_dir/out.txt")
lmk_before=$(get_mtime "$lmk_dir/out.txt")
assert_eq "$(run_make_status "$make_dir" all)" "0" "make up-to-date run"
assert_eq "$(run_lmk_status "$lmk_dir" basic all)" "0" "libmake up-to-date run"
make_after=$(get_mtime "$make_dir/out.txt")
lmk_after=$(get_mtime "$lmk_dir/out.txt")
assert_eq "$make_before" "$make_after" "make rebuilt unexpectedly"
assert_eq "$lmk_before" "$lmk_after" "libmake rebuilt unexpectedly"

sleep 1
echo "v2" > "$make_dir/in.txt"
echo "v2" > "$lmk_dir/in.txt"
assert_eq "$(run_make_status "$make_dir" all)" "0" "make stale rebuild"
assert_eq "$(run_lmk_status "$lmk_dir" basic all)" "0" "libmake stale rebuild"
make_new=$(get_mtime "$make_dir/out.txt")
lmk_new=$(get_mtime "$lmk_dir/out.txt")
[ "$make_new" -gt "$make_after" ] || fail "make did not rebuild stale target"
[ "$lmk_new" -gt "$lmk_after" ] || fail "libmake did not rebuild stale target"

echo "[3/4] missing-target error behavior"
base_missing="$tmp_root/base-missing"
mkdir -p "$base_missing"
cat > "$base_missing/Makefile" <<'MK'
all: ; @:
MK
make_missing="$tmp_root/make-missing"
lmk_missing="$tmp_root/lmk-missing"
cp -R "$base_missing" "$make_missing"
cp -R "$base_missing" "$lmk_missing"

make_status=$(run_make_status "$make_missing" missing)
lmk_status=$(run_lmk_status "$lmk_missing" missing missing)
[ "$make_status" -ne 0 ] || fail "make missing target should fail"
[ "$lmk_status" -ne 0 ] || fail "libmake missing target should fail"

echo "[4/4] recipe failure behavior"
base_fail="$tmp_root/base-fail"
mkdir -p "$base_fail"
cat > "$base_fail/Makefile" <<'MK'
all: bad

bad: ; sh -c 'exit 7'
MK
make_fail="$tmp_root/make-fail"
lmk_fail="$tmp_root/lmk-fail"
cp -R "$base_fail" "$make_fail"
cp -R "$base_fail" "$lmk_fail"

make_status=$(run_make_status "$make_fail" all)
lmk_status=$(run_lmk_status "$lmk_fail" fail all)
[ "$make_status" -ne 0 ] || fail "make failing recipe should fail"
[ "$lmk_status" -ne 0 ] || fail "libmake failing recipe should fail"

echo "PASS: core differential checks succeeded"
