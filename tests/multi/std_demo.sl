; uses lib/std.sl: strings, printing, memcpy/memset, strcmp
section .data:
  i8[] hello = 'Hello from std\0'
  i8[] same1 = 'abc\0'
  i8[] same2 = 'abc\0'
  i8[] diff  = 'abd\0'
  u8[16] copy
end
section .text:
define global i32 _start:
entry:
  push ptr @hello
  call print_str()
  call print_nl()

  push i64 0
  call print_int()
  call print_nl()
  push i64 1234567890123
  call print_int()
  call print_nl()
  push i64 -42
  call print_int()
  call print_nl()
  push i64 -9223372036854775808
  call print_int()
  call print_nl()

  push ptr @hello
  call strlen()
  call print_int()
  call print_nl()

  push ptr @same1
  push ptr @same2
  call strcmp()
  sext i64
  call print_int()
  call print_nl()
  push ptr @same1
  push ptr @diff
  call strcmp()
  sext i64
  call print_int()
  call print_nl()

  push ptr @copy
  push ptr @hello
  push i64 15
  call memcpy()
  push ptr @copy
  call print_str()
  call print_nl()
  push ptr @copy
  push u8 90
  push i64 5
  call memset()
  push ptr @copy
  call print_str()
  call print_nl()

  push i32 7
  syscall exit
end
end
end
