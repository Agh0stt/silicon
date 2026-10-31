section .data:
  global i32[2] a = {1, 2}
  global u8 b = 1
end
section .text:
define global i32 _start:
entry:
  push i64 1
  push i64 2
  index i32
  pop
  push i32 0
  syscall exit
end
end
end
