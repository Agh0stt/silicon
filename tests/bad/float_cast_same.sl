section .text:
define global i32 _start:
entry:
  push f64 1.0
  cast f64
  pop
  push i32 0
  syscall exit
end
end
end
