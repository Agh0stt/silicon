; auto-generated float stress test: exit code = number of the first failing check (0 = all pass)
section .data:
  global f64[3] fa = {1.5, 2.5, 3.5}
  global f64 g = 3.25
end
section .text:
define global f64 mix(f64 a, i32 b, f64 c):
entry:
  push f64 a
  push i32 b
  cast f64
  * f64
  push f64 c
  + f64
  ret
end
end
define global f32 sq(f32 x):
entry:
  push f32 x
  push f32 x
  * f32
  ret
end
end
define global i32 _start:
  local f64 zero = 0.0
  local f64 nan
  local f32 x32 = 2.5
entry:
  push f64 zero
  push f64 zero
  / f64
  pop nan
  ; 1: f32 0.1+0.2==0.3 (true in single precision)
  push f32 0.1
  push f32 0.2
  + f32
  push f32 0.3
  == f32
  !
  if:
    push i32 1
    syscall exit
  end
  ; 2: f64 0.1+0.2!=0.3
  push f64 0.1
  push f64 0.2
  + f64
  push f64 0.3
  != f64
  !
  if:
    push i32 2
    syscall exit
  end
  ; 3: NaN == NaN is false
  push f64 nan
  push f64 nan
  == f64
  !
  !
  if:
    push i32 3
    syscall exit
  end
  ; 4: NaN != NaN is true
  push f64 nan
  push f64 nan
  != f64
  !
  if:
    push i32 4
    syscall exit
  end
  ; 5: NaN < 1 false
  push f64 nan
  push f64 1.0
  < f64
  !
  !
  if:
    push i32 5
    syscall exit
  end
  ; 6: NaN > 1 false
  push f64 nan
  push f64 1.0
  > f64
  !
  !
  if:
    push i32 6
    syscall exit
  end
  ; 7: NaN <= 1 false
  push f64 nan
  push f64 1.0
  <= f64
  !
  !
  if:
    push i32 7
    syscall exit
  end
  ; 8: NaN >= 1 false
  push f64 nan
  push f64 1.0
  >= f64
  !
  !
  if:
    push i32 8
    syscall exit
  end
  ; 9: 1/0 = inf > 1e308
  push f64 1.0
  push f64 zero
  / f64
  push f64 1e308
  > f64
  !
  if:
    push i32 9
    syscall exit
  end
  ; 10: cast -2.9 -> i32 == -2 (truncate toward zero)
  push f64 -2.9
  cast i32
  push i32 -2
  == i32
  !
  if:
    push i32 10
    syscall exit
  end
  ; 11: cast 2.9 -> i32 == 2
  push f64 2.9
  cast i32
  push i32 2
  == i32
  !
  if:
    push i32 11
    syscall exit
  end
  ; 12: cast i32 7 -> f64
  push i32 7
  cast f64
  push f64 7.0
  == f64
  !
  if:
    push i32 12
    syscall exit
  end
  ; 13: cast u32 4000000000 -> f64 (zero-extend)
  push u32 4000000000
  cast f64
  push f64 4000000000.0
  == f64
  !
  if:
    push i32 13
    syscall exit
  end
  ; 14: cast f32 1.5 -> f64
  push f32 1.5
  cast f64
  push f64 1.5
  == f64
  !
  if:
    push i32 14
    syscall exit
  end
  ; 15: cast f64 0.1 -> f32 matches f32 literal
  push f64 0.1
  cast f32
  push f32 0.1
  == f32
  !
  if:
    push i32 15
    syscall exit
  end
  ; 16: neg f64 0.0 == 0.0
  push f64 0.0
  neg f64
  push f64 0.0
  == f64
  !
  if:
    push i32 16
    syscall exit
  end
  ; 17: neg f32 2.5 == -2.5
  push f32 2.5
  neg f32
  push f32 -2.5
  == f32
  !
  if:
    push i32 17
    syscall exit
  end
  ; 18: f64 array sum 1.5+2.5+3.5 == 7.5
  push ptr @fa
  push i64 0
  index f64
  load f64
  push ptr @fa
  push i64 1
  index f64
  load f64
  + f64
  push ptr @fa
  push i64 2
  index f64
  load f64
  + f64
  push f64 7.5
  == f64
  !
  if:
    push i32 18
    syscall exit
  end
  ; 19: store 10.25 into fa[1], load it back
  push ptr @fa
  push i64 1
  index f64
  push f64 10.25
  store f64
  push ptr @fa
  push i64 1
  index f64
  load f64
  push f64 10.25
  == f64
  !
  if:
    push i32 19
    syscall exit
  end
  ; 20: f64/i32/f64 params -> f64 return: mix(1.5,4,0.5)=6.5
  call mix(f64 1.5, i32 4, f64 0.5)
  push f64 6.5
  == f64
  !
  if:
    push i32 20
    syscall exit
  end
  ; 21: f32 param/return: sq(1.5)=2.25
  call sq(f32 1.5)
  push f32 2.25
  == f32
  !
  if:
    push i32 21
    syscall exit
  end
  ; 22: f32 1.0 < 2.0
  push f32 1.0
  push f32 2.0
  < f32
  !
  if:
    push i32 22
    syscall exit
  end
  ; 23: f32 2.0 < 1.0 false
  push f32 2.0
  push f32 1.0
  < f32
  !
  !
  if:
    push i32 23
    syscall exit
  end
  ; 24: f64 2.0 <= 2.0
  push f64 2.0
  push f64 2.0
  <= f64
  !
  if:
    push i32 24
    syscall exit
  end
  ; 25: f64 2.0 >= 2.0
  push f64 2.0
  push f64 2.0
  >= f64
  !
  if:
    push i32 25
    syscall exit
  end
  ; 26: f32 3.0 > 2.0
  push f32 3.0
  push f32 2.0
  > f32
  !
  if:
    push i32 26
    syscall exit
  end
  ; 27: f32 1/3 == (f32)(1.0/3.0)
  push f32 1.0
  push f32 3.0
  / f32
  push f64 1.0
  push f64 3.0
  / f64
  cast f32
  == f32
  !
  if:
    push i32 27
    syscall exit
  end
  ; 28: local f32 initializer
  push f32 x32
  push f32 2.5
  == f32
  !
  if:
    push i32 28
    syscall exit
  end
  ; 29: global f64 initializer 3.25
  push f64 g
  push f64 3.25
  == f64
  !
  if:
    push i32 29
    syscall exit
  end
  ; 30: f64 2.5*4.0-1.0 == 9.0
  push f64 2.5
  push f64 4.0
  * f64
  push f64 1.0
  - f64
  push f64 9.0
  == f64
  !
  if:
    push i32 30
    syscall exit
  end
  ; 31: -0.0 literal == 0.0
  push f64 -0.0
  push f64 0.0
  == f64
  !
  if:
    push i32 31
    syscall exit
  end
  ; 32: 1e300*1e300 = inf > 1e308
  push f64 1e300
  push f64 1e300
  * f64
  push f64 1e308
  > f64
  !
  if:
    push i32 32
    syscall exit
  end
  ; 33: f32 sub/div: (10.0-4.0)/4.0 == 1.5
  push f32 10.0
  push f32 4.0
  - f32
  push f32 4.0
  / f32
  push f32 1.5
  == f32
  !
  if:
    push i32 33
    syscall exit
  end
  ; 34: i64 -> f64 big: 9007199254740992 exact
  push i64 9007199254740992
  cast f64
  push f64 9007199254740992.0
  == f64
  !
  if:
    push i32 34
    syscall exit
  end
  ; 35: f64 -> i64 trunc of -1e10
  push f64 -1e10
  cast i64
  push i64 -10000000000
  == i64
  !
  if:
    push i32 35
    syscall exit
  end
  push i32 0
  syscall exit
end
end
end