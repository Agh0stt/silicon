; _start ends with std's noreturn exit(): no terminator needed after it
section .text:
define global i32 _start:
entry:
  call exit(i32 3)
end
end
end
