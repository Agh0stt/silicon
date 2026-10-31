; itoa: convert 4096 to decimal text in a buffer, print it with write
section .data:
  global u8[24] buf
end
section .text:
define global i32 _start:
  local u64 n = 4096
  local i64 pos = 23
entry:
  push ptr @buf
  push i64 23
  index u8
  push u8 10          ; '\n' at the end
  store u8
  loop:
    -- pos
    push ptr @buf
    push i64 pos
    index u8          ; &buf[pos]
    push u64 n
    push u64 10
    % u64
    trunc u8
    push u8 48
    + u8
    store u8          ; buf[pos] = '0' + n % 10
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
  push i32 0
  syscall exit
end
end
end
