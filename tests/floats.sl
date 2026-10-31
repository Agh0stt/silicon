; f32/f64: arithmetic, compares (incl. NaN), neg, casts, globals, arrays, params/returns.
; exit code = number of true checks (expect 20)
section .data:
  global i32 hits = 0
  global f64 g = 2.5
  global f32 h = 1.5
  global f64[3] arr = {1.0, 2.0, 4}
end
section .text:

define f64 half(f64 x):
entry:
  push f64 x
  push f64 2.0
  / f64
  ret
end
end

define f32 scale(f32 x, f32 k):
entry:
  push f32 x
  push f32 k
  * f32
  ret
end
end

define global i32 _start:
  local f64 nan
  local f64 acc = 0.0
  local i64 i = 0
entry:
  ; 1: 1.5 + 2.25 == 3.75
  push f64 1.5
  push f64 2.25
  + f64
  push f64 3.75
  == f64
  if:
    ++ hits
  end
  ; 2: 10 - 0.5 == 9.5
  push f64 10
  push f64 0.5
  - f64
  push f64 9.5
  == f64
  if:
    ++ hits
  end
  ; 3: 3 * -2.5 == -7.5   (negative literal)
  push f64 3.0
  push f64 -2.5
  * f64
  push f64 -7.5
  == f64
  if:
    ++ hits
  end
  ; 4: half(9) == 4.5   (param and return)
  call half(f64 9.0)
  push f64 4.5
  == f64
  if:
    ++ hits
  end
  ; 5: f32 call: scale(1.5, 4) == 6
  call scale(f32 1.5, f32 4.0)
  push f32 6.0
  == f32
  if:
    ++ hits
  end
  ; 6-9: ordering, f64
  push f64 1.0
  push f64 2.0
  < f64
  if:
    ++ hits
  end
  push f64 2.0
  push f64 2.0
  <= f64
  if:
    ++ hits
  end
  push f64 3.0
  push f64 2.0
  > f64
  if:
    ++ hits
  end
  push f64 2.0
  push f64 2.0
  >= f64
  if:
    ++ hits
  end
  ; 10: negative below positive (signed compare, not bit compare)
  push f64 -1.0
  push f64 1.0
  < f64
  if:
    ++ hits
  end
  ; 11: neg f64
  push f64 6.25
  neg f64
  push f64 -6.25
  == f64
  if:
    ++ hits
  end
  ; 12: neg f32
  push f32 0.5
  neg f32
  push f32 -0.5
  == f32
  if:
    ++ hits
  end
  ; build NaN = 0/0
  push f64 0.0
  push f64 0.0
  / f64
  pop nan
  ; 13: NaN != NaN is true
  push f64 nan
  push f64 nan
  != f64
  if:
    ++ hits
  end
  ; 14: NaN == NaN is false
  push f64 nan
  push f64 nan
  == f64
  not
  if:
    ++ hits
  end
  ; 15: NaN < 1 is false, and 1 < NaN is false
  push f64 nan
  push f64 1.0
  < f64
  push f64 1.0
  push f64 nan
  < f64
  or
  not
  if:
    ++ hits
  end
  ; 16: global f64 + global f32 via casts: g(2.5) + h(1.5->f64) == 4.0
  push f64 g
  push f32 h
  cast f64
  + f64
  push f64 4.0
  == f64
  if:
    ++ hits
  end
  ; 17: int -> float -> int: 7 -> 7.0, *1.5 = 10.5, trunc -> 10
  push i32 7
  cast f64
  push f64 1.5
  * f64
  cast i32
  push i32 10
  == i32
  if:
    ++ hits
  end
  ; 18: float -> int truncates toward zero for negatives: -2.9 -> -2
  push f64 -2.9
  cast i64
  push i64 -2
  == i64
  if:
    ++ hits
  end
  ; 19: sum the global array with load/index: 1 + 2 + 4 = 7
  loop:
    push i64 i
    push i64 3
    >= i64
    if:
      break
    end
    push f64 acc
    push ptr @arr
    push i64 i
    index f64
    load f64
    + f64
    pop acc
    ++ i
  end
  push f64 acc
  push f64 7.0
  == f64
  if:
    ++ hits
  end
  ; 20: store through a pointer into the array, read it back
  push ptr @arr
  push i64 1
  index f64
  push f64 0.125
  store f64
  push ptr @arr
  push i64 1
  index f64
  load f64
  push f64 0.125
  == f64
  if:
    ++ hits
  end
  push i32 hits
  syscall exit
end
end
end
