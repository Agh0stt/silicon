; each recursion level has its own buffer; buf[0]=depth must survive the deeper call.
; also checks locals are zero-filled. depth(3): returns 3+2+1+0 = 6, plus 0 if zeroed -> exit 6
section .text:
define i32 depth(i32 d):
  local i32[4] buf
entry:
  ; buf[3] must start zeroed
  push ptr @buf
  push i64 3
  index i32
  load i32
  push i32 0
  != i32
  if:
    push i32 100
    ret
  end
  push ptr @buf
  push i64 0
  index i32
  push i32 d
  store i32
  push i32 d
  push i32 0
  == i32
  if:
    push i32 0
    ret
  end
  push i32 d
  push i32 1
  - i32
  call depth()
  pop
  ; after the deeper call, our buf[0] must still equal d
  push ptr @buf
  push i64 0
  index i32
  load i32
  push i32 d
  != i32
  if:
    push i32 100
    ret
  end
  push i32 d
  push i32 d
  push i32 1
  - i32
  call depth()
  + i32
  ret
end
end

define global i32 _start:
entry:
  call depth(i32 3)
  syscall exit
end
end
end
