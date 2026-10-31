#!/bin/sh
# Builds silicon, then runs every test at both -O0 and -O1 (they must agree),
# checks must-reject programs, and runs a short random-program fuzz against a Python reference.
cd "$(dirname "$0")"
gcc -O2 -Wall -Wextra -o silicon silicon.c || exit 1
fails=0
check() { # flags name expected_exit [expected_stdout]
  fl=$1; n=$2; want=$3
  if ! ./silicon tests/$n.sl -o /tmp/sl_$n $fl 2>/dev/null; then echo "FAIL $n $fl (did not compile)"; fails=$((fails+1)); return; fi
  out=$(/tmp/sl_$n); got=$?
  if [ "$got" != "$want" ]; then echo "FAIL $n $fl (exit $got, want $want)"; fails=$((fails+1)); return; fi
  if [ $# -ge 4 ] && [ "$out" != "$4" ]; then echo "FAIL $n $fl (stdout '$out', want '$4')"; fails=$((fails+1)); return; fi
  echo "PASS $n $fl (exit $got)"
}
for fl in -O1 -O0; do
check $fl hello 0 "Hello World"
check $fl call 5
check $fl sum 55
check $fl fact 120
check $fl fib 55
check $fl signs 6
check $fl labels 7
check $fl itoa 0 "4096"
check $fl arrays 112
check $fl casts 7
check $fl itoa_local 0 "0
7
1234567890
18446744073709551615"
check $fl local_arrays 6
check $fl floats 20
check $fl u64float 0
check $fl manyargs 42
check $fl float_print 0 "3.142
-2.500
0.000
1234.500
2.500"
check $fl float_stress 0
done
for f in tests/hello_user.sl tests/bad/*.sl; do
  rm -f /tmp/sl_bad
  if ./silicon $f -o /tmp/sl_bad 2>/tmp/sl_err; then echo "FAIL $(basename $f) should be rejected"; fails=$((fails+1))
  elif [ -e /tmp/sl_bad ]; then echo "FAIL $(basename $f) rejected but wrote output"; fails=$((fails+1))
  else echo "PASS rejected $(basename $f): $(sed 's/^[^:]*:[0-9]*: error: //' /tmp/sl_err | head -1)"; fi
done
# ---- multi-file programs (lib/std.sl and visibility rules) ----
for fl in -O1 -O0; do
  ./silicon tests/multi/std_demo.sl lib/std.sl -o /tmp/sl_std $fl 2>/dev/null; out=$(/tmp/sl_std); got=$?
  exp="Hello from std
0
1234567890123
-42
-9223372036854775808
14
0
-1
Hello from std
ZZZZZ from std"
  if [ "$got" = 7 ] && [ "$out" = "$exp" ]; then echo "PASS std_demo $fl (exit 7, output matches)"; else echo "FAIL std_demo $fl (exit $got)"; fails=$((fails+1)); fi
  ./silicon tests/multi/exit_test.sl lib/std.sl -o /tmp/sl_ex $fl 2>/dev/null; /tmp/sl_ex; got=$?
  [ "$got" = 5 ] && echo "PASS exit_test $fl (exit 5)" || { echo "FAIL exit_test $fl (exit $got)"; fails=$((fails+1)); }
  ./silicon tests/multi/priv_ok_a.sl tests/multi/priv_ok_b.sl -o /tmp/sl_po $fl 2>/dev/null; /tmp/sl_po; got=$?
  [ "$got" = 7 ] && echo "PASS priv_ok $fl (two private 'helper's, exit 7)" || { echo "FAIL priv_ok $fl (exit $got)"; fails=$((fails+1)); }
done
for t in bad_private_fn bad_private_var bad_dup_global bad_dup_start; do
  rm -f /tmp/sl_bad
  if ./silicon tests/multi/${t}_a.sl tests/multi/${t}_b.sl -o /tmp/sl_bad 2>/tmp/sl_err; then echo "FAIL $t should be rejected"; fails=$((fails+1))
  elif [ -e /tmp/sl_bad ]; then echo "FAIL $t rejected but wrote output"; fails=$((fails+1))
  else echo "PASS rejected $t: $(sed 's/^[^:]*:[0-9]*: error: //' /tmp/sl_err | head -1)"; fi
done
if command -v python3 >/dev/null; then
  r=$(python3 tests/fuzz.py 150 | tail -1); echo "$r"
  case "$r" in *" 0 mismatches") ;; *) fails=$((fails+1));; esac
fi
# ---- C interop: -c object output linked with gcc (floats in xmm, alignment, externs, separate compilation) ----
if command -v gcc >/dev/null; then
  ./silicon -c tests/interop/lib.sl -o /tmp/sl_lib.o 2>/dev/null && gcc -o /tmp/sl_interop tests/interop/main.c /tmp/sl_lib.o -lm 2>/tmp/sl_err
  out=$(/tmp/sl_interop 2>&1); got=$?
  if [ "$got" = 7 ] && echo "$out" | grep -q "interop: all ok" && echo "$out" | grep -q "printf: 42 .* 2.50"; then echo "PASS interop (C calls Silicon, Silicon calls libc, exit 7)"
  else echo "FAIL interop (exit $got): $out"; fails=$((fails+1)); fi
  ./silicon -c tests/interop/cmain.sl -o /tmp/sl_cmain.o 2>/dev/null && gcc -o /tmp/sl_cmain /tmp/sl_cmain.o -lm
  out=$(/tmp/sl_cmain); got=$?
  want="x=1024.000 n=1 s=silicon
x=0.500 n=7 s=silicon"
  if [ "$got" = 0 ] && [ "$out" = "$want" ]; then echo "PASS cmain (Silicon main + libc, odd/even stack depth)"; else echo "FAIL cmain (exit $got)"; fails=$((fails+1)); fi
  ./silicon -c tests/interop/sepA.sl -o /tmp/sl_sa.o 2>/dev/null; ./silicon -c tests/interop/sepB.sl -o /tmp/sl_sb.o 2>/dev/null
  gcc -o /tmp/sl_sep /tmp/sl_sa.o /tmp/sl_sb.o && /tmp/sl_sep; got=$?
  [ "$got" = 0 ] && echo "PASS separate compilation (two Silicon objects)" || { echo "FAIL separate compilation (exit $got)"; fails=$((fails+1)); }
  if gcc -no-pie -o /tmp/sl_interop2 tests/interop/main.c /tmp/sl_lib.o -lm 2>/dev/null && /tmp/sl_interop2 >/dev/null 2>&1; [ $? = 7 ]; then echo "PASS interop -no-pie"; else echo "FAIL interop -no-pie"; fails=$((fails+1)); fi
else echo "SKIP interop (no gcc)"; fi
# ---- .rodata / const ----
for fl in -O0 -O1; do check $fl rodata 64 "const hello"; done
./silicon tests/rodata.sl -o /tmp/sl_ro 2>/dev/null
if readelf -lW /tmp/sl_ro 2>/dev/null | grep -E "LOAD" | grep -q " RW " && ! readelf -lW /tmp/sl_ro | grep LOAD | grep -q "RWE\|R E.*W\|WE"; then echo "PASS rodata segments (R+X code, R rodata, RW data, none W+X)"; else echo "FAIL rodata segments"; fails=$((fails+1)); fi
./silicon tests/rodata_write.sl -o /tmp/sl_rw 2>/dev/null; /tmp/sl_rw 2>/dev/null; got=$?
[ "$got" = 139 ] && echo "PASS write into .rodata faults (SIGSEGV)" || { echo "FAIL write into .rodata (exit $got)"; fails=$((fails+1)); }
if command -v gcc >/dev/null; then
  ./silicon -c tests/interop/ro.sl -o /tmp/sl_roo.o 2>/dev/null && gcc -no-pie -o /tmp/sl_rom tests/interop/ro_main.c /tmp/sl_roo.o 2>/dev/null
  [ "$(/tmp/sl_rom)" = "ro: ok" ] && echo "PASS .rodata object (read-only section, relocation inside it)" || { echo "FAIL .rodata object"; fails=$((fails+1)); }
fi
# ---- structs ----
for fl in -O0 -O1; do check $fl structs 8; done
if command -v gcc >/dev/null; then
  ./silicon -c tests/interop/st.sl -o /tmp/sl_st.o 2>/dev/null && gcc -o /tmp/sl_stm tests/interop/st_main.c /tmp/sl_st.o
  [ "$(/tmp/sl_stm)" = "struct layout: matches C" ] && echo "PASS struct layout matches C (sizes, 8 offsets, read/write through ptr)" || { echo "FAIL struct layout vs C"; fails=$((fails+1)); }
fi
# ---- noreturn functions ----
for fl in -O0 -O1; do
  ./silicon tests/noret/ok.sl -o /tmp/sl_nr $fl 2>/dev/null; /tmp/sl_nr; got=$?
  [ "$got" = 7 ] && echo "PASS noret ok $fl (exit 7)" || { echo "FAIL noret ok $fl (exit $got)"; fails=$((fails+1)); }
  ./silicon tests/multi/exit_end.sl lib/std.sl -o /tmp/sl_ee $fl 2>/dev/null; /tmp/sl_ee; got=$?
  [ "$got" = 3 ] && echo "PASS exit_end $fl (exit 3)" || { echo "FAIL exit_end $fl (exit $got)"; fails=$((fails+1)); }
done
for f in tests/noret/bad_*.sl; do
  rm -f /tmp/sl_bad
  if ./silicon $f -o /tmp/sl_bad 2>/tmp/sl_err; then echo "FAIL $(basename $f) should be rejected"; fails=$((fails+1))
  elif [ -e /tmp/sl_bad ]; then echo "FAIL $(basename $f) rejected but wrote output"; fails=$((fails+1))
  else echo "PASS rejected $(basename $f): $(sed 's/^[^:]*:[0-9]*: error: //' /tmp/sl_err | head -1)"; fi
done
# ---- std additions: print_u64/print_hex/parse_int/read_line/alloc/free ----
for fl in -O0 -O1; do
  ./silicon tests/multi/std2.sl lib/std.sl -o /tmp/sl_s2 $fl 2>/dev/null
  out=$(printf '  line one\n-123x\n' | /tmp/sl_s2); got=$?
  want="18446744073709551615
0xff
0x0
0xdeadbeef
12345
-987
0
10
  line one
-123"
  if [ "$got" = 7 ] && [ "$out" = "$want" ]; then echo "PASS std2 $fl"; else echo "FAIL std2 $fl (exit $got)"; fails=$((fails+1)); fi
done
[ $fails = 0 ] && echo "ALL TESTS PASSED" || { echo "$fails FAILED"; exit 1; }
