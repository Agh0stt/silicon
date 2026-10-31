section .text:
define noreturn i32 f():
entry:
  push i32 1
  syscall exit
end
end
define global i32 _start:
entry:
  push i32 0
  syscall exit
end
end
end
