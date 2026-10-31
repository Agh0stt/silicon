; labeled blocks with br / brif: count to 7
section .text:
define global i32 _start:
  local i32 i = 0
entry:
  br check
end
check:
  push i32 i
  push i32 7
  < i32
  brif body, done
end
body:
  ++ i
  br check
end
done:
  push i32 i
  syscall exit
end
end
end
