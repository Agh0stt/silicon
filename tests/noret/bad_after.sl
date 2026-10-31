section .text:
define noreturn void f():
entry:
  push i32 1
  syscall exit
end
end
define global i32 _start:
entry:
  call f()
  push i32 0
  syscall exit
end
end
end
