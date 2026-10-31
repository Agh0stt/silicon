; hello.sl

section .data:
global i8[11] r0 = 'Hello World'
i8 ptr msg = @r0
end
section .text:
define global i32 _start:
entry:
push i32 1
push ptr msg
push i64 11
syscall write
pop
push i32 0
syscall exit
end
end
end
