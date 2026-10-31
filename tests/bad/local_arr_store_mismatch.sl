section .text:
define global i32 _start:
  local i32[4] b
entry:
  push ptr @b
  push i64 1
  index i32
  push i8 1
  store i32
  push i32 0
  syscall exit
end
end
end
