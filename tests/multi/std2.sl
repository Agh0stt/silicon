; tests for print_u64, print_hex, parse_int, read_line, alloc/free (stdin: "  line one\n-123x\n")
section .data:
  i8[] n1 = '12345\0'
  i8[] n2 = '-987\0'
  i8[] n3 = 'abc\0'
end
section .text:
define global i32 _start:
  local u8[64] buf
  local ptr heap = 0
entry:
  push u64 18446744073709551615
  call print_u64()
  call print_nl()
  push u64 255
  call print_hex()
  call print_nl()
  push u64 0
  call print_hex()
  call print_nl()
  push u64 3735928559
  call print_hex()
  call print_nl()
  push ptr @n1
  call parse_int()
  call print_int()
  call print_nl()
  push ptr @n2
  call parse_int()
  call print_int()
  call print_nl()
  push ptr @n3
  call parse_int()
  call print_int()
  call print_nl()
  push ptr @buf
  push i64 64
  call read_line()
  call print_int()
  call print_nl()
  push ptr @buf
  call print_str()
  call print_nl()
  push ptr @buf
  push i64 64
  call read_line()
  pop
  push ptr @buf
  call parse_int()
  call print_int()
  call print_nl()
  push i64 100000
  call alloc()
  pop heap
  push ptr heap
  push i64 99999
  index u8
  push u8 7
  store u8
  push ptr heap
  push i64 5
  index u8
  load u8
  zext i32
  push ptr heap
  push i64 99999
  index u8
  load u8
  zext i32
  + i32
  syscall exit
end
end
end
