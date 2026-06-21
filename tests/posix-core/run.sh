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

run_lmk_embed_status()
{
dir=$1
scenario=$2
target=${3:-}
set +e
(
cd "$dir"
if [ "$target" ]; then
"$runner" build "$scenario" "$target" >/dev/null 2>&1
else
"$runner" build "$scenario" >/dev/null 2>&1
fi
)
status=$?
set -e
echo "$status"
}

emit_scenario_makefile()
{
dir=$1
scenario=$2
(
cd "$dir"
"$runner" emit "$scenario" > Makefile
)
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

echo "[1/6] building libmake differential runners"
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

echo "[2/6] embedded graph emission: basic build + stale rebuild"
base_basic="$tmp_root/base-basic"
mkdir -p "$base_basic"
echo "v1" > "$base_basic/in.txt"

make_dir="$tmp_root/make-basic"
cli_dir="$tmp_root/cli-basic"
embed_dir="$tmp_root/embed-basic"
copy_fixture "$base_basic" "$make_dir"
copy_fixture "$base_basic" "$cli_dir"
copy_fixture "$base_basic" "$embed_dir"
emit_scenario_makefile "$make_dir" basic
emit_scenario_makefile "$cli_dir" basic
emit_scenario_makefile "$embed_dir" basic

assert_eq "$(run_make_status "$make_dir" all)" "0" "make basic initial build"
assert_eq "$(run_lmk_cli_status "$cli_dir" all)" "0" "libmake CLI initial build"
assert_eq "$(run_lmk_embed_status "$embed_dir" basic all)" "0" \
"embedded libmake initial build"

assert_eq "$(cat "$make_dir/out.txt")" "$(cat "$cli_dir/out.txt")" \
"make and CLI output mismatch"
assert_eq "$(cat "$make_dir/out.txt")" "$(cat "$embed_dir/out.txt")" \
"make and embedded output mismatch"

make_before=$(get_mtime "$make_dir/out.txt")
cli_before=$(get_mtime "$cli_dir/out.txt")
embed_before=$(get_mtime "$embed_dir/out.txt")
assert_eq "$(run_make_status "$make_dir" all)" "0" "make up-to-date run"
assert_eq "$(run_lmk_cli_status "$cli_dir" all)" "0" "CLI up-to-date run"
assert_eq "$(run_lmk_embed_status "$embed_dir" basic all)" "0" \
"embedded up-to-date run"
assert_eq "$make_before" "$(get_mtime "$make_dir/out.txt")" \
"make rebuilt unexpectedly"
assert_eq "$cli_before" "$(get_mtime "$cli_dir/out.txt")" \
"CLI rebuilt unexpectedly"
assert_eq "$embed_before" "$(get_mtime "$embed_dir/out.txt")" \
"embedded rebuilt unexpectedly"

sleep 1
echo "v2" > "$make_dir/in.txt"
echo "v2" > "$cli_dir/in.txt"
echo "v2" > "$embed_dir/in.txt"
assert_eq "$(run_make_status "$make_dir" all)" "0" "make stale rebuild"
assert_eq "$(run_lmk_cli_status "$cli_dir" all)" "0" "CLI stale rebuild"
assert_eq "$(run_lmk_embed_status "$embed_dir" basic all)" "0" \
"embedded stale rebuild"
[ "$(get_mtime "$make_dir/out.txt")" -gt "$make_before" ] ||
fail "make did not rebuild stale target"
[ "$(get_mtime "$cli_dir/out.txt")" -gt "$cli_before" ] ||
fail "CLI did not rebuild stale target"
[ "$(get_mtime "$embed_dir/out.txt")" -gt "$embed_before" ] ||
fail "embedded libmake did not rebuild stale target"

echo "[3/6] embedded graph emission: repeated rules and shared dependencies"
base_complex="$tmp_root/base-complex"
mkdir -p "$base_complex"
echo "input" > "$base_complex/input.txt"
echo "input" > "$base_complex/input"

make_repeated="$tmp_root/make-repeated"
cli_repeated="$tmp_root/cli-repeated"
embed_repeated="$tmp_root/embed-repeated"
copy_fixture "$base_complex" "$make_repeated"
copy_fixture "$base_complex" "$cli_repeated"
copy_fixture "$base_complex" "$embed_repeated"
emit_scenario_makefile "$make_repeated" repeated
emit_scenario_makefile "$cli_repeated" repeated
emit_scenario_makefile "$embed_repeated" repeated

assert_eq "$(run_make_status "$make_repeated")" "0" "make repeated default"
assert_eq "$(run_lmk_cli_status "$cli_repeated")" "0" "CLI repeated default"
assert_eq "$(run_lmk_embed_status "$embed_repeated" repeated)" "0" \
"embedded repeated default"
assert_eq "$(cat "$make_repeated/first")" "$(cat "$cli_repeated/first")" \
"repeated first output mismatch"
assert_eq "$(cat "$make_repeated/first")" "$(cat "$embed_repeated/first")" \
"embedded repeated first output mismatch"
assert_eq "$(cat "$make_repeated/second")" "$(cat "$cli_repeated/second")" \
"repeated second output mismatch"
assert_eq "$(cat "$make_repeated/second")" "$(cat "$embed_repeated/second")" \
"embedded repeated second output mismatch"

make_shared="$tmp_root/make-shared"
cli_shared="$tmp_root/cli-shared"
embed_shared="$tmp_root/embed-shared"
copy_fixture "$base_complex" "$make_shared"
copy_fixture "$base_complex" "$cli_shared"
copy_fixture "$base_complex" "$embed_shared"
emit_scenario_makefile "$make_shared" shared
emit_scenario_makefile "$cli_shared" shared
emit_scenario_makefile "$embed_shared" shared

assert_eq "$(run_make_status "$make_shared" all)" "0" "make shared build"
assert_eq "$(run_lmk_cli_status "$cli_shared" all)" "0" "CLI shared build"
assert_eq "$(run_lmk_embed_status "$embed_shared" shared all)" "0" \
"embedded shared build"
assert_eq "$(cat "$make_shared/left")" "$(cat "$cli_shared/left")" \
"shared left output mismatch"
assert_eq "$(cat "$make_shared/left")" "$(cat "$embed_shared/left")" \
"embedded shared left output mismatch"
assert_eq "$(cat "$make_shared/right")" "$(cat "$cli_shared/right")" \
"shared right output mismatch"
assert_eq "$(cat "$make_shared/right")" "$(cat "$embed_shared/right")" \
"embedded shared right output mismatch"

echo "[4/6] embedded graph emission: failure paths"
base_empty="$tmp_root/base-empty"
mkdir -p "$base_empty"

make_missing="$tmp_root/make-missing"
cli_missing="$tmp_root/cli-missing"
embed_missing="$tmp_root/embed-missing"
copy_fixture "$base_empty" "$make_missing"
copy_fixture "$base_empty" "$cli_missing"
copy_fixture "$base_empty" "$embed_missing"
emit_scenario_makefile "$make_missing" basic
emit_scenario_makefile "$cli_missing" basic

make_status=$(run_make_status "$make_missing" missing)
cli_status=$(run_lmk_cli_status "$cli_missing" missing)
embed_status=$(run_lmk_embed_status "$embed_missing" basic missing)
[ "$make_status" -ne 0 ] || fail "make missing target should fail"
[ "$cli_status" -ne 0 ] || fail "CLI missing target should fail"
[ "$embed_status" -ne 0 ] || fail "embedded missing target should fail"

make_dep="$tmp_root/make-missing-dep"
cli_dep="$tmp_root/cli-missing-dep"
embed_dep="$tmp_root/embed-missing-dep"
copy_fixture "$base_empty" "$make_dep"
copy_fixture "$base_empty" "$cli_dep"
copy_fixture "$base_empty" "$embed_dep"
emit_scenario_makefile "$make_dep" missing-dep
emit_scenario_makefile "$cli_dep" missing-dep

make_status=$(run_make_status "$make_dep" all)
cli_status=$(run_lmk_cli_status "$cli_dep" all)
embed_status=$(run_lmk_embed_status "$embed_dep" missing-dep all)
[ "$make_status" -ne 0 ] || fail "make missing dependency should fail"
[ "$cli_status" -ne 0 ] || fail "CLI missing dependency should fail"
[ "$embed_status" -ne 0 ] || fail "embedded missing dependency should fail"

make_fail="$tmp_root/make-fail"
cli_fail="$tmp_root/cli-fail"
embed_fail="$tmp_root/embed-fail"
copy_fixture "$base_empty" "$make_fail"
copy_fixture "$base_empty" "$cli_fail"
copy_fixture "$base_empty" "$embed_fail"
emit_scenario_makefile "$make_fail" fail
emit_scenario_makefile "$cli_fail" fail

make_status=$(run_make_status "$make_fail" all)
cli_status=$(run_lmk_cli_status "$cli_fail" all)
embed_status=$(run_lmk_embed_status "$embed_fail" fail all)
[ "$make_status" -ne 0 ] || fail "make failing recipe should fail"
[ "$cli_status" -ne 0 ] || fail "CLI failing recipe should fail"
[ "$embed_status" -ne 0 ] || fail "embedded failing recipe should fail"

echo "[5/6] parser fixtures remain secondary"
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
cli_parser="$tmp_root/cli-parser"
copy_fixture "$base_parser" "$make_parser"
copy_fixture "$base_parser" "$cli_parser"

assert_eq "$(run_make_status "$make_parser")" "0" "make parser default"
assert_eq "$(run_lmk_cli_status "$cli_parser")" "0" "CLI parser default"
assert_eq "$(cat "$make_parser/first")" "$(cat "$cli_parser/first")" \
"parser first output mismatch"
assert_eq "$(cat "$make_parser/second")" "$(cat "$cli_parser/second")" \
"parser second output mismatch"

base_space="$tmp_root/base-space-parser"
mkdir -p "$base_space"
cat > "$base_space/Makefile" <<'MK'
all: out

out: my input
	cat my input > out
MK
echo "my" > "$base_space/my"
echo "input" > "$base_space/input"

make_space="$tmp_root/make-space-parser"
cli_space="$tmp_root/cli-space-parser"
copy_fixture "$base_space" "$make_space"
copy_fixture "$base_space" "$cli_space"

assert_eq "$(run_make_status "$make_space" all)" "0" \
"make whitespace parser fixture"
assert_eq "$(run_lmk_cli_status "$cli_space" all)" "0" \
"CLI whitespace parser fixture"
assert_eq "$(cat "$make_space/out")" "$(cat "$cli_space/out")" \
"whitespace parser output mismatch"

if "$runner" emit space-name > "$tmp_root/space-name.mk" 2>/dev/null; then
fail "embedded graph with internal-space dependency should not emit Makefile"
fi
[ ! -s "$tmp_root/space-name.mk" ] ||
fail "failed internal-space emission should not write Makefile content"

echo "[6/6] provider CLI can emit embedded Makefile"
"$libmake" --dump-makefile > "$tmp_root/provider.mk"
grep -E '^all: libmake$' "$tmp_root/provider.mk" >/dev/null
grep -E '^libmake: main\.o dag\.o exec\.o libmake\.o$' \
"$tmp_root/provider.mk" >/dev/null

echo "PASS: core differential checks succeeded"
