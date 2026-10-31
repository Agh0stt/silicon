section .text:
define global i64 triple(i64 x):
entry:
  push i64 x
  push i64 3
  mul i64
  ret
end
end
define global f64 half(f64 x):
entry:
  push f64 x
  push f64 2.0
  div f64
  ret
end
end
end
