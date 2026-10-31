section .text:
define global i32 _start:
  local i8[] b
entry:
  push i32 0
  pop
  push i32 0
  syscall exit
end
end
end
