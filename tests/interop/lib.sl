; Silicon side of the C interop test. Build with: silicon -c lib.sl -o lib.o
section .data:
  i8[] msg = 'hello from silicon via puts\0'
  i8[] fmt = 'printf: %d %s %.2f\n\0'
  global i64 counter = 5
  global i32[4] table = {10, 20, 30, 40}
  i64 hidden = 99
  ptr msgp = @msg
end
section .text:
extern i32 puts(ptr s)
extern i32 printf(ptr fmt, i32 a, ptr s, f64 x)
extern f64 sqrt(f64 x)
extern f32 sqrtf(f32 x)
extern u64 strlen(ptr s)
extern i32 abs(i32 x)
extern noreturn void exit(i32 code)

define global i32 say:
entry:
  push ptr @msg
  call puts()
  pop
  push ptr @fmt
  push i32 42
  push ptr @msg
  push f64 2.5
  call printf()
  pop
  push i32 0
  ret
end
end

; floats cross the C boundary in xmm registers
define global f64 hyp(f64 a, f64 b):
entry:
  push f64 a
  push f64 a
  mul f64
  push f64 b
  push f64 b
  mul f64
  add f64
  call sqrt()
  ret
end
end

define global f32 root32(f32 x):
entry:
  push f32 x
  call sqrtf()
  ret
end
end

; narrow ints, mixed int/float, and stack-passed arguments (9 ints + 9 floats)
define global i64 mixed(i8 a, f64 b, u16 c, f32 d, i32 e, i64 f, i64 g, i64 h, i64 i, i64 j, f64 k, f64 l, f64 m, f64 n, f64 o, f64 p, f64 q, f64 r):
entry:
  ; return a + c + e + f + j, ignoring the floats, to prove the int args line up
  push i8 a
  sext i64
  push u16 c
  zext i64
  add i64
  push i32 e
  sext i64
  add i64
  push i64 f
  add i64
  push i64 j
  add i64
  ret
end
end

define global i64 length:
local i64 n
entry:
  push ptr @msg
  call strlen()
  cast i64
  pop n
  push i64 n
  ret
end
end

define global i32 absval(i32 x):
entry:
  push i32 x
  call abs()
  ret
end
end

define global i64 bump:
entry:
  push i64 counter
  inc counter
  push i64 hidden
  add i64
  ret
end
end

define global void die_with(i32 code):
entry:
  push i32 code
  call exit()
end
end
end
