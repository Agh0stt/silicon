; a const needs a value, since nothing can set it later
section .rodata:
  i64 limit
end
section .text:
define global i32 _start:
entry:
  push i32 0
  syscall exit
end
end
end
