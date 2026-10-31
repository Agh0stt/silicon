section .data:
  global u8[2] a = {1, 300}
end
section .text:
define global i32 _start:
entry:
  push i32 0
  syscall exit
end
end
end
