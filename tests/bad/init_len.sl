section .data:
  global i32[3] a = {1, 2}
end
section .text:
define global i32 _start:
entry:
  push i32 0
  syscall exit
end
end
end
