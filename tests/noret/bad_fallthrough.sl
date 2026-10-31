section .text:
define noreturn void f():
entry:
  push i32 1
  pop
end
end
define global i32 _start:
entry:
  call f()
end
end
end
