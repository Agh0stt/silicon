section .text:
define global i32 foo():
entry:
  push i32 1
  ret
end
end
define global i32 _start:
entry:
  push i32 0
  syscall exit
end
end
end
