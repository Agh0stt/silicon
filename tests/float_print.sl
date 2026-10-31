; prints f64 values with 3 decimals using only f64 ops, casts and a local stack buffer
section .text:
define void print_f64(f64 x):
  local u8[40] buf
  local i64 pos = 39
  local i64 k = 0
  local u64 n = 0
  local i32 isneg = 0
entry:
  push ptr @buf
  push i64 39
  index u8
  push u8 10
  store u8
  push f64 x
  push f64 0.0
  < f64
  if:
    push i32 1
    pop isneg
    push f64 x
    neg f64
    pop x
  end
  ; n = round(x * 1000), as an unsigned integer (u64 <-> float goes through i64)
  push f64 x
  push f64 1000.0
  * f64
  push f64 0.5
  + f64
  cast i64
  cast u64
  pop n
  loop:
    -- pos
    push i64 k
    push i64 3
    == i64
    if:
      push ptr @buf
      push i64 pos
      index u8
      push u8 46            ; '.'
      store u8
      ++ k
      continue
    end
    push ptr @buf
    push i64 pos
    index u8
    push u64 n
    push u64 10
    % u64
    trunc u8
    push u8 48
    + u8
    store u8
    push u64 n
    push u64 10
    / u64
    pop n
    ++ k
    push u64 n
    push u64 0
    == u64
    push i64 k
    push i64 3
    > i64
    and
    if:
      break
    end
  end
  push i32 isneg
  push i32 0
  != i32
  if:
    -- pos
    push ptr @buf
    push i64 pos
    index u8
    push u8 45              ; '-'
    store u8
  end
  push i32 1
  push ptr @buf
  push i64 pos
  index u8
  push i64 40
  push i64 pos
  - i64
  syscall write
  pop
  ret
end
end

define global i32 _start:
entry:
  call print_f64(f64 3.14159)
  call print_f64(f64 -2.5)
  call print_f64(f64 0.0)
  call print_f64(f64 1234.5)
  push f32 2.5
  cast f64
  call print_f64()
  push i32 0
  syscall exit
end
end
end
