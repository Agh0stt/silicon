; .rodata: constant tables and strings, read-only at run time
section .rodata:
  i32[5] squares = {1, 4, 9, 16, 25}
  i8[] greeting = 'const hello\n'
  ptr where = @squares
end
section .data:
  i64 total
end
section .text:
define global i32 _start:
local i64 i
entry:
  loop:
    push i64 i
    push i64 5
    ge i64
    if:
      break
    end
    push i64 total
    push ptr @squares
    push i64 i
    index i32
    load i32
    sext i64
    add i64
    pop total
    inc i
  end
  push i32 1
  push ptr @greeting
  push i64 12
  syscall write
  pop
  ; read back through the ptr stored in .rodata: squares[2] = 9
  push i64 total
  push ptr where
  push i64 2
  index i32
  load i32
  sext i64
  add i64
  trunc i32
  syscall exit
end
end
end
