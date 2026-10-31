; extern calls are type-checked like any other call
section .text:
extern f64 sqrt(f64 x)
define global i32 _start:
entry:
  push i64 4
  call sqrt()
  pop
  push i32 0
  syscall exit
end
end
end
