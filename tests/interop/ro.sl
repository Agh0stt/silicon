; read-only data exported to C, including a ptr table that needs a relocation inside .rodata
section .rodata:
  i8[] a = 'alpha\0'
  i8[] b = 'beta\0'
  global ptr[2] names = {0, 0}
  global i32[3] primes = {2, 3, 5}
  global ptr first = @a
end
section .data:
  global i32 mutable = 1
end
section .text:
define global i32 get_prime(i32 k):
entry:
  push ptr @primes
  push i32 k
  sext i64
  index i32
  load i32
  ret
end
end
end
