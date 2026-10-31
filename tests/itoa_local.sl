; itoa into a *local* stack buffer; function prints any u64 followed by newline
section .text:
define void print_u64(u64 n):
  local u8[24] buf
  local i64 pos = 23
entry:
  push ptr @buf
  push i64 23
  index u8
  push u8 10
  store u8
  loop:
    -- pos
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
    push u64 n
    push u64 0
    == u64
    if:
      break
    end
  end
  push i32 1
  push ptr @buf
  push i64 pos
  index u8
  push i64 24
  push i64 pos
  - i64
  syscall write
  pop
  ret
end
end

define global i32 _start:
entry:
  call print_u64(u64 0)
  call print_u64(u64 7)
  call print_u64(u64 1234567890)
  call print_u64(u64 18446744073709551615)
  push i32 0
  syscall exit
end
end
end
