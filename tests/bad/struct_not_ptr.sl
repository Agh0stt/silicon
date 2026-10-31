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
  push i64 5
  getf P.x
  pop
  push i32 0
  syscall exit
end
end
end
