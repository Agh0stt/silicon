section .text:
define noreturn void f():
entry:
  ret
end
end
define global i32 _start:
entry:
  call f()
end
end
end
