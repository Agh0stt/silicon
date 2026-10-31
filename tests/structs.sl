; structs: C-compatible layout, getf/setf/field/sizeof, struct arrays, local and global instances
section .types:
  struct Point:
    i32 x
    i32 y
  end
  struct Rec:
    u8 tag
    i64 id
    u16 n
    f64 w
    u8[5] name
    ptr next
  end
end
section .data:
  Point origin
  Rec[3] recs
end
section .text:
define i32 sum_points(ptr pts, i64 n):
local i64 i
local i32 total
entry:
  loop:
    push i64 i
    push i64 n
    ge i64
    if:
      break
    end
    push i32 total
    push ptr pts
    push i64 i
    index Point
    getf Point.x
    add i32
    push ptr pts
    push i64 i
    index Point
    getf Point.y
    add i32
    pop total
    inc i
  end
  push i32 total
  ret
end
end

define global i32 _start:
local Point[4] pts
local Rec r
local i64 k
entry:
  ; fill pts[k] = (k+1, 10*(k+1))
  loop:
    push i64 k
    push i64 4
    ge i64
    if:
      break
    end
    push ptr @pts
    push i64 k
    index Point
    push i64 k
    push i64 1
    add i64
    trunc i32
    setf Point.x
    push ptr @pts
    push i64 k
    index Point
    push i64 k
    push i64 1
    add i64
    push i64 10
    mul i64
    trunc i32
    setf Point.y
    inc k
  end
  ; Rec layout (C rules): tag@0 id@8 n@16 w@24 name@32 next@40 size 48
  push ptr @r
  push u8 7
  setf Rec.tag
  push ptr @r
  push i64 1000000000000
  setf Rec.id
  push ptr @r
  push u16 513
  setf Rec.n
  push ptr @r
  push f64 2.5
  setf Rec.w
  push ptr @r
  field Rec.name
  push i64 2
  index u8
  push u8 90
  store u8
  ; global struct and array element
  push ptr @origin
  push i32 -5
  setf Point.x
  push ptr @recs
  push i64 2
  index Rec
  push u8 4
  setf Rec.tag
  ; checks, each failing with a distinct exit code
  call sum_points(ptr @pts, i64 4)       ; 1+2+3+4 + 10+20+30+40 = 110
  push i32 110
  ne i32
  if:
    push i32 1
    syscall exit
  end
  push ptr @r
  getf Rec.id
  push i64 1000000000000
  ne i64
  if:
    push i32 2
    syscall exit
  end
  push ptr @r
  getf Rec.w
  push f64 2.5
  ne f64
  if:
    push i32 3
    syscall exit
  end
  push ptr @r
  field Rec.name
  push i64 2
  index u8
  load u8
  push u8 90
  ne u8
  if:
    push i32 4
    syscall exit
  end
  push ptr @r
  field Rec.next
  cast i64
  push ptr @r
  cast i64
  sub i64
  push i64 40
  ne i64
  if:
    push i32 5
    syscall exit
  end
  sizeof Rec
  push u64 48
  ne u64
  if:
    push i32 6
    syscall exit
  end
  push ptr @recs
  push i64 2
  index Rec
  getf Rec.tag
  push u8 4
  ne u8
  if:
    push i32 7
    syscall exit
  end
  push ptr @origin
  getf Point.x
  push i32 -5
  ne i32
  if:
    push i32 8
    syscall exit
  end
  push ptr @r
  getf Rec.n
  push u16 513
  ne u16
  if:
    push i32 9
    syscall exit
  end
  sizeof Point
  trunc i32
  syscall exit
end
end
end
