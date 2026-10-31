section .text:
define i32 helper():
entry:
  push i32 4
  ret
end
end
define global i32 get_b():
entry:
  call helper()
  ret
end
end
end
