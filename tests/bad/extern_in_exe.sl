; extern functions can only be used when writing an object file (-c)
section .text:
extern i32 abs(i32 x)
define global i32 _start:
entry:
  push i32 -3
  call abs()
  syscall exit
end
end
end
