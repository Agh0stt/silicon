; signed vs unsigned: exit code = number of true checks (expect 6)
section .data:
  global i32 hits = 0
end
section .text:
define global i32 _start:
  local i32 s = -7
  local u32 u = 4000000000
entry:
  ; -7 < 3 signed -> true
  push i32 s
  push i32 3
  < i32
  if:
    ++ hits
  end
  ; 4000000000 > 3 unsigned -> true
  push u32 u
  push u32 3
  > u32
  if:
    ++ hits
  end
  ; -7 / 2 = -3 (signed idiv)
  push i32 s
  push i32 2
  / i32
  push i32 -3
  == i32
  if:
    ++ hits
  end
  ; -7 % 2 = -1
  push i32 s
  push i32 2
  % i32
  push i32 -1
  == i32
  if:
    ++ hits
  end
  ; i8 wraparound: 127 + 1 = -128
  push i8 127
  push i8 1
  + i8
  push i8 -128
  == i8
  if:
    ++ hits
  end
  ; bool logic: !(false) && (true || false)
  push bool false
  !
  push bool true
  push bool false
  ||
  &&
  if:
    ++ hits
  end
  push i32 hits
  syscall exit
end
end
end
