; recursion + if/else: fact(5)=120
section .text:
define global i32 fact(i32 n):
entry:
  push i32 n
  push i32 1
  <= i32
  if:
    push i32 1
    ret
  end
  else:
    push i32 n
    push i32 n
    push i32 1
    - i32
    call fact()
    * i32
    ret
  end
end
end

define global i32 _start:
entry:
  call fact(i32 5)
  syscall exit
end
end
end
