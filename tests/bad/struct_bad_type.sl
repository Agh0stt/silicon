section .types:
  struct P:
    i32 x
    u8[4] tag
  end
end
section .text:
define global i32 _start:
local P p
entry:
  push ptr @p
  push i64 1
  setf P.x
  push i32 0
  syscall exit
end
end
end
