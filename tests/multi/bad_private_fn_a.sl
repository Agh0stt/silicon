section .text:
define global i32 _start:
entry:
  call hidden()
  syscall exit
end
end
end
