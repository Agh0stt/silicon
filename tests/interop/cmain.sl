; a whole program in Silicon, linked by gcc against libc: gcc cmain.o -lm
; calls printf with float args while the value stack has an odd depth underneath
; (misaligned rsp would crash inside printf's SSE code)
section .data:
  i8[] fmt = 'x=%.3f n=%d s=%s\n\0'
  i8[] name = 'silicon\0'
end
section .text:
extern i32 printf(ptr fmt, f64 x, i32 n, ptr s)
extern f64 pow(f64 a, f64 b)

define global i32 main(i32 argc, ptr argv):
entry:
  push i64 111                 ; one value stays on the stack below the call
  push ptr @fmt
  push f64 2.0
  push f64 10.0
  call pow()
  push i32 argc
  push ptr @name
  call printf()
  pop
  push i64 222                 ; now two values: even depth
  push ptr @fmt
  push f64 0.5
  push i32 7
  push ptr @name
  call printf()
  pop
  pop
  pop
  push i32 argc
  push i32 1
  sub i32
  ret
end
end
end
