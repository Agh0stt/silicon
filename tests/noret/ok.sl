; noreturn: nothing needed after call exit()
section .text:
define noreturn void die(i32 code):
entry:
  push i32 code
  syscall exit
end
end
define global i32 _start:
entry:
  push i32 1
  push i32 1
  == i32
  if:
    call die(i32 7)
  end
  push i32 9
  syscall exit
end
end
end
