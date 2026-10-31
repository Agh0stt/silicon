section .text:
define global i32 _start:
  local i8[8] b
entry:
  push i8 1
  pop b
  push i32 0
  syscall exit
end
end
end
