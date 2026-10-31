; separate compilation: A calls a function defined in B
section .text:
extern i64 triple(i64 x)
extern f64 half(f64 x)
define global i32 main:
entry:
  push i64 14
  call triple()
  push i64 42
  sub i64
  trunc i32
  ret
end
end
end
