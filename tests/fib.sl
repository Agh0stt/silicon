; recursive fib(10)=55 using names instead of symbols
section .text:
define global i32 fib(i32 n):
entry:
  push i32 n
  push i32 2
  lt i32
  if:
    push i32 n
    ret
  end
  push i32 n
  push i32 1
  sub i32
  call fib()
  push i32 n
  push i32 2
  sub i32
  call fib()
  add i32
  ret
end
end

define global i32 _start:
entry:
  call fib(i32 10)
  syscall exit
end
end
end
