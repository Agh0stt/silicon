; {..} initializers, index/load/store with i32 and i64 elements
; sum of nums = 1+2+3+4+5 = 15, then store 100 into nums[2] and add -> 15-3+100 = 112
section .data:
  global i32[5] nums = {1, 2, 3, 4, 5}
  global i64[3] big = {-1, 0x100000000, 7}
end
section .text:
define global i32 _start:
  local i64 i = 0
  local i32 total = 0
entry:
  loop:
    push i64 i
    push i64 5
    >= i64
    if:
      break
    end
    push i32 total
    push ptr @nums
    push i64 i
    index i32
    load i32
    + i32
    pop total
    ++ i
  end
  push ptr @nums
  push i64 2
  index i32
  push i32 100
  store i32
  push i32 total
  push ptr @nums
  push i64 2
  index i32
  load i32
  + i32
  push i32 3
  - i32
  pop total
  ; big[0] == -1 and big[1] == 4294967296 (8-byte elements)
  push ptr @big
  push i64 0
  index i64
  load i64
  push i64 -1
  == i64
  push ptr @big
  push i64 1
  index i64
  load i64
  push i64 4294967296
  == i64
  &&
  if:
    push i32 total
    syscall exit
  end
  push i32 1
  syscall exit
end
end
end
