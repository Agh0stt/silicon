; a .rodata variable can never be assigned
section .rodata:
  i64 limit = 10
end
section .text:
define global i32 _start:
entry:
  push i64 20
  pop limit
  push i32 0
  syscall exit
end
end
end
