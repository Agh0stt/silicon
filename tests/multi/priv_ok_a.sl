; each file has its own private 'helper'; exit = 3 + 4
section .text:
define i32 helper():
entry:
  push i32 3
  ret
end
end
define global i32 _start:
entry:
  call helper()
  call get_b()
  + i32
  syscall exit
end
end
end
