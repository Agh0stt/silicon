section .rodata:
  i64 limit = 10
end
section .text:
define global i32 _start:
entry:
  inc limit
  push i32 0
  syscall exit
end
end
end
