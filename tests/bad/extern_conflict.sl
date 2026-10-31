; the same extern declared twice with different signatures
section .text:
extern i32 abs(i32 x)
extern i64 abs(i64 x)
define global i32 _start:
entry:
  push i32 0
  syscall exit
end
end
end
