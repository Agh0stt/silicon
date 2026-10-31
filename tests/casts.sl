; sext/zext/trunc/cast: exit code = number of true checks (expect 7)
section .data:
  global i32 hits = 0
end
section .text:
define global i32 _start:
entry:
  push i8 -1
  sext i64
  push i64 -1
  == i64
  if:
    ++ hits
  end
  push u8 255
  zext u64
  push u64 255
  == u64
  if:
    ++ hits
  end
  push i32 300
  trunc u8             ; 300 & 255 = 44
  push u8 44
  == u8
  if:
    ++ hits
  end
  push i32 -1
  cast u32             ; reinterpret -> 4294967295
  push u32 4294967295
  == u32
  if:
    ++ hits
  end
  push bool true
  zext i32
  push i32 1
  == i32
  if:
    ++ hits
  end
  push i64 -2
  trunc i32
  push i32 -2
  == i32
  if:
    ++ hits
  end
  push ptr @hits
  cast u64
  cast ptr
  load i32             ; hits is 6 here
  push i32 6
  == i32
  if:
    ++ hits
  end
  push i32 hits
  syscall exit
end
end
end
