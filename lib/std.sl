; std.sl - Silicon standard library (Linux x86-64). Compile it together with your program:
;     silicon main.sl lib/std.sl -o main
; Everything here is `global`; helpers are private to this file.

section .text:

; ---- process ----------------------------------------------------------
define global noreturn void exit(i32 code):
entry:
  push i32 code
  syscall exit_group
end
end

; ---- strings & memory -------------------------------------------------
; length of a NUL-terminated string
define global i64 strlen(ptr s):
  local i64 n = 0
entry:
  loop:
    push ptr s
    push i64 n
    index u8
    load u8
    push u8 0
    == u8
    if:
      break
    end
    ++ n
  end
  push i64 n
  ret
end
end

; 0 if equal, else the difference of the first differing bytes
define global i32 strcmp(ptr a, ptr b):
  local i64 i = 0
  local u8 x = 0
  local u8 y = 0
entry:
  loop:
    push ptr a
    push i64 i
    index u8
    load u8
    pop x
    push ptr b
    push i64 i
    index u8
    load u8
    pop y
    push u8 x
    push u8 y
    != u8
    if:
      push u8 x
      zext i32
      push u8 y
      zext i32
      - i32
      ret
    end
    push u8 x
    push u8 0
    == u8
    if:
      push i32 0
      ret
    end
    ++ i
  end
end
end

define global void memcpy(ptr dst, ptr src, i64 n):
  local i64 i = 0
entry:
  loop:
    push i64 i
    push i64 n
    >= i64
    if:
      break
    end
    push ptr dst
    push i64 i
    index u8
    push ptr src
    push i64 i
    index u8
    load u8
    store u8
    ++ i
  end
  ret
end
end

define global void memset(ptr p, u8 v, i64 n):
  local i64 i = 0
entry:
  loop:
    push i64 i
    push i64 n
    >= i64
    if:
      break
    end
    push ptr p
    push i64 i
    index u8
    push u8 v
    store u8
    ++ i
  end
  ret
end
end

; ---- output (stdout) --------------------------------------------------
define global void print_buf(ptr s, i64 n):
entry:
  push i32 1
  push ptr s
  push i64 n
  syscall write
  pop
  ret
end
end

define global void print_str(ptr s):
entry:
  push ptr s
  push ptr s
  call strlen()
  call print_buf()
  ret
end
end

define global void print_char(u8 c):
  local u8[1] b
entry:
  push ptr @b
  push u8 c
  store u8
  push ptr @b
  push i64 1
  call print_buf()
  ret
end
end

define global void print_nl():
entry:
  push u8 10
  call print_char()
  ret
end
end

; signed decimal, handles the full i64 range including the minimum value
define global void print_int(i64 n):
  local u8[24] buf
  local i64 pos = 23
  local u64 v = 0
  local bool isneg = false
entry:
  push i64 n
  push i64 0
  < i64
  if:
    push bool true
    pop isneg
    push i64 n
    neg i64
    cast u64
    pop v
  end
  else:
    push i64 n
    cast u64
    pop v
  end
  loop:
    push ptr @buf
    push i64 pos
    index u8
    push u64 v
    push u64 10
    % u64
    trunc u8
    push u8 48
    + u8
    store u8
    push u64 v
    push u64 10
    / u64
    pop v
    -- pos
    push u64 v
    push u64 0
    == u64
    if:
      break
    end
  end
  push bool isneg
  if:
    push ptr @buf
    push i64 pos
    index u8
    push u8 45
    store u8
    -- pos
  end
  push ptr @buf
  push i64 pos
  push i64 1
  + i64
  index u8
  push i64 23
  push i64 pos
  - i64
  call print_buf()
  ret
end
end
; ---- more output ------------------------------------------------------
; unsigned decimal
define global void print_u64(u64 n):
  local u8[24] buf
  local i64 pos = 24
entry:
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
  push ptr @buf
  push i64 pos
  index u8
  push i64 24
  push i64 pos
  - i64
  call print_buf()
  ret
end
end

; hexadecimal with a 0x prefix, lowercase, no leading zeros
define global void print_hex(u64 n):
  local u8[18] buf
  local i64 pos = 18
  local u8 d = 0
entry:
  loop:
    -- pos
    push u64 n
    push u64 16
    % u64
    trunc u8
    push u8 48
    + u8
    pop d
    push u8 d
    push u8 58
    >= u8
    if:
      push u8 d
      push u8 39
      + u8
      pop d
    end
    push ptr @buf
    push i64 pos
    index u8
    push u8 d
    store u8
    push u64 n
    push u64 16
    / u64
    pop n
    push u64 n
    push u64 0
    == u64
    if:
      break
    end
  end
  -- pos
  push ptr @buf
  push i64 pos
  index u8
  push u8 120
  store u8
  -- pos
  push ptr @buf
  push i64 pos
  index u8
  push u8 48
  store u8
  push ptr @buf
  push i64 pos
  index u8
  push i64 18
  push i64 pos
  - i64
  call print_buf()
  ret
end
end

; ---- input ------------------------------------------------------------
; Reads one line from stdin into buf (room for cap bytes, cap >= 1), without the newline,
; and NUL-terminates it. Returns the length, or -1 at end of input with nothing read.
define global i64 read_line(ptr buf, i64 cap):
  local i64 n = 0
  local i64 r = 0
  local u8[1] c
entry:
  loop:
    push i64 n
    push i64 cap
    push i64 1
    - i64
    >= i64
    if:
      break
    end
    push i32 0
    push ptr @c
    push i64 1
    syscall read
    pop r
    push i64 r
    push i64 1
    != i64
    if:
      push i64 n
      push i64 0
      == i64
      if:
        push ptr buf
        push i64 0
        index u8
        push u8 0
        store u8
        push i64 -1
        ret
      end
      break
    end
    push ptr @c
    load u8
    push u8 10
    == u8
    if:
      break
    end
    push ptr buf
    push i64 n
    index u8
    push ptr @c
    load u8
    store u8
    ++ n
  end
  push ptr buf
  push i64 n
  index u8
  push u8 0
  store u8
  push i64 n
  ret
end
end

; Parses an optional '-' followed by decimal digits; stops at the first other character.
; No overflow check: out-of-range values wrap.
define global i64 parse_int(ptr s):
  local i64 i = 0
  local i64 v = 0
  local i64 isneg = 0
  local u8 c = 0
entry:
  push ptr s
  push i64 0
  index u8
  load u8
  push u8 45
  == u8
  if:
    push i64 1
    pop isneg
    ++ i
  end
  loop:
    push ptr s
    push i64 i
    index u8
    load u8
    pop c
    push u8 c
    push u8 48
    < u8
    if:
      break
    end
    push u8 c
    push u8 57
    > u8
    if:
      break
    end
    push i64 v
    push i64 10
    * i64
    push u8 c
    push u8 48
    - u8
    zext i64
    + i64
    pop v
    ++ i
  end
  push i64 isneg
  push i64 0
  != i64
  if:
    push i64 v
    neg i64
    ret
  end
  push i64 v
  ret
end
end

; ---- memory -----------------------------------------------------------
; alloc(n): n bytes of zeroed memory (one mmap per call, rounded up to whole pages).
; Returns 0 (null) on failure. Release with free().
define global ptr alloc(i64 n):
  local i64 total = 0
  local ptr p = 0
entry:
  push i64 n
  push i64 4103
  + i64
  push i64 4096
  / i64
  push i64 4096
  * i64
  pop total
  push ptr 0
  push i64 total
  push i32 3
  push i32 34
  push i32 -1
  push i64 0
  syscall mmap
  pop p
  push ptr p
  cast i64
  push i64 0
  < i64
  if:
    push ptr 0
    ret
  end
  push ptr p
  push i64 total
  store i64
  push ptr p
  push i64 8
  index u8
  ret
end
end

define global void free(ptr p):
  local ptr base = 0
  local i64 total = 0
entry:
  push ptr p
  push ptr 0
  == ptr
  if:
    ret
  end
  push ptr p
  push i64 -8
  index u8
  pop base
  push ptr base
  load i64
  pop total
  push ptr base
  push i64 total
  syscall munmap
  pop
  ret
end
end
end
