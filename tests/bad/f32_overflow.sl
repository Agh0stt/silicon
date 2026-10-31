section .text:
define global i32 _start:
entry:
  push f32 1e39
  pop
  push i32 0
  syscall exit
end
end
end
