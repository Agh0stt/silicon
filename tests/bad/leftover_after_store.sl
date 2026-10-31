section .data:
  global i32[2] a = {1, 2}
  global u8 b = 1
end
section .text:
define global i32 _start:
entry:
  push ptr @a
  push i32 1
  push i32 2
  store i32
  push i32 0
  syscall exit
end
end
end
