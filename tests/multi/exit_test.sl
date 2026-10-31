; std's exit() called from inside an if: (exit code 5); the code after it must not run
section .text:
define global i32 _start:
entry:
  push i32 1
  push i32 1
  == i32
  if:
    push i32 5
    call exit()
  end
  push i32 9
  syscall exit
end
end
end
