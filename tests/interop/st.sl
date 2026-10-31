; the same struct as in st_main.c: layout must match C exactly
section .types:
  struct Mixed:
    i8 a
    f32 b
    u16 c
    f64 d
    bool e
    i64 f
    u8[3] g
    ptr h
  end
end
section .text:
define global u64 mixed_size:
entry:
  sizeof Mixed
  ret
end
end

; offset of every field, by taking its address relative to the base
define global i64 offset_of(ptr base, i32 which):
local i64 r
entry:
  push i32 which
  push i32 0
  eq i32
  if:
    push ptr base
    field Mixed.a
    cast i64
    push ptr base
    cast i64
    sub i64
    ret
  end
  push i32 which
  push i32 1
  eq i32
  if:
    push ptr base
    field Mixed.b
    cast i64
    push ptr base
    cast i64
    sub i64
    ret
  end
  push i32 which
  push i32 2
  eq i32
  if:
    push ptr base
    field Mixed.c
    cast i64
    push ptr base
    cast i64
    sub i64
    ret
  end
  push i32 which
  push i32 3
  eq i32
  if:
    push ptr base
    field Mixed.d
    cast i64
    push ptr base
    cast i64
    sub i64
    ret
  end
  push i32 which
  push i32 4
  eq i32
  if:
    push ptr base
    field Mixed.e
    cast i64
    push ptr base
    cast i64
    sub i64
    ret
  end
  push i32 which
  push i32 5
  eq i32
  if:
    push ptr base
    field Mixed.f
    cast i64
    push ptr base
    cast i64
    sub i64
    ret
  end
  push i32 which
  push i32 6
  eq i32
  if:
    push ptr base
    field Mixed.g
    cast i64
    push ptr base
    cast i64
    sub i64
    ret
  end
  push ptr base
  field Mixed.h
  cast i64
  push ptr base
  cast i64
  sub i64
  ret
end
end

; read C-written fields and write some back
define global f64 sum_fields(ptr m):
entry:
  push ptr m
  getf Mixed.d
  push ptr m
  getf Mixed.b
  cast f64
  add f64
  ret
end
end
define global void fill(ptr m):
entry:
  push ptr m
  push i8 -7
  setf Mixed.a
  push ptr m
  push u16 65000
  setf Mixed.c
  push ptr m
  push bool true
  setf Mixed.e
  push ptr m
  push i64 -123456789012
  setf Mixed.f
  ret
end
end
end
