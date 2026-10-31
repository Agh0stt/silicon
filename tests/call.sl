section .text:
define global i32 plus(i32 a, i32 b):
entry:
  push i32 a
  push i32 b
  + i32
  ret
end
end

define global i32 _start:
entry:
  call plus(i32 2, i32 3)
  syscall exit
end
end
end
