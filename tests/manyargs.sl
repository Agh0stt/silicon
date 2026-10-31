; functions with more than 6 parameters (7th+ go on the stack)
section .text:
define i64 weighted(i64 a, i64 b, i64 c, i64 d, i64 e, i64 f, i64 g, i64 h, i64 i):
entry:
  push i64 a
  push i64 b
  push i64 2
  mul i64
  add i64
  push i64 c
  push i64 3
  mul i64
  add i64
  push i64 d
  push i64 4
  mul i64
  add i64
  push i64 e
  push i64 5
  mul i64
  add i64
  push i64 f
  push i64 6
  mul i64
  add i64
  push i64 g
  push i64 7
  mul i64
  add i64
  push i64 h
  push i64 8
  mul i64
  add i64
  push i64 i
  push i64 9
  mul i64
  add i64
  ret
end
end
; recursion with 8 params, one of them reassigned
define i64 count(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 acc, i64 step):
entry:
  push i64 n
  push i64 0
  == i64
  if:
    push i64 acc
    ret
  end
  push i64 acc
  push i64 step
  add i64
  pop acc
  push i64 n
  push i64 1
  sub i64
  pop n
  call count(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 acc, i64 step)
  ret
end
end
define global i32 _start:
entry:
  call weighted(i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7, i64 8, i64 9)
  push i64 285
  != i64
  if:
    push i32 1
    syscall exit
  end
  call count(i64 10, i64 1, i64 2, i64 3, i64 4, i64 5, i64 0, i64 3)
  push i64 30
  != i64
  if:
    push i32 2
    syscall exit
  end
  push i32 42
  syscall exit
end
end
end
