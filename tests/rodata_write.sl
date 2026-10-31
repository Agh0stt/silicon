; writing through a pointer into .rodata is not caught at compile time, but the page is read-only: SIGSEGV
section .rodata:
  i32[2] t = {1, 2}
end
section .text:
define global i32 _start:
entry:
  push ptr @t
  push i32 99
  store i32
  push i32 0
  syscall exit
end
end
end
