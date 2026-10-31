; sum 1..n with the structured loop, expect 55
section .text:
define global i32 sum_to(i32 n):
  local i32 i = 1
  local i32 total = 0
entry:
  loop:
    push i32 i
    push i32 n
    > i32
    if:
      break
    end
    push i32 total
    push i32 i
    + i32
    pop total
    ++ i
  end
  push i32 total
  ret
end
end

define global i32 _start:
entry:
  call sum_to(i32 10)
  syscall exit
end
end
end
