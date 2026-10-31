; u64 <-> float casts, including values with the top bit set (>= 2^63)
section .data:
global u64[4] big = {18446744073709551615, 9223372036854775808, 9223372036854775809, 12345}
end
section .text:
define global i32 _start:
  local i32 bad = 0
  local u64 v
  local f64 d
entry:
  ; 12345 -> f64 -> u64 round trip
  push ptr @big
  push i32 3
  index u64
  load u64
  cast f64
  cast u64
  push u64 12345
  != u64
  if:
    ++ bad
  end
  ; 2^63 -> f64 -> u64 must give back 2^63
  push ptr @big
  push i32 1
  index u64
  load u64
  cast f64
  cast u64
  push u64 9223372036854775808
  != u64
  if:
    ++ bad
  end
  ; 2^63 as f64 compares equal to 9223372036854775808.0
  push ptr @big
  push i32 1
  index u64
  load u64
  cast f64
  push f64 9223372036854775808.0
  != f64
  if:
    ++ bad
  end
  ; u64 max -> f32 is 2^64 (1.8446744e19); back to u64 is out of range, so only check f64 path of 2^63+1 rounds to 2^63
  push ptr @big
  push i32 2
  index u64
  load u64
  cast f64
  cast u64
  push u64 9223372036854775808
  != u64
  if:
    ++ bad
  end
  ; f32: 3e9 fits in u64 and is above i32 range
  push f32 3000000000.0
  cast u64
  push u64 3000000000
  != u64
  if:
    ++ bad
  end
  ; float >= 2^63 -> u64
  push f64 1.8e19
  cast u64
  push u64 18000000000000000000
  != u64
  if:
    ++ bad
  end
  push i32 bad
  syscall exit
end
end
end
