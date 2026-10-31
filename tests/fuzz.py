import random, subprocess, sys
M=(1<<64)-1
def s64(v):
    v&=M
    return v-(1<<64) if v>>63 else v
def wrap(v,bits,signed):
    v&=(1<<bits)-1
    return v-(1<<bits) if signed and v>>(bits-1) else v
CONSTS=[0,1,2,3,7,10,100,255,256,-1,-2,-7,-100,65535,2147483647,-2147483648,2147483648,5000000000,-5000000000,123456789012]
def gen(d,env,rnd):
    # returns (code_lines, value)
    if d==0 or rnd.random()<0.18:
        if rnd.random()<0.5:
            c=rnd.choice(CONSTS) if rnd.random()<0.7 else rnd.randint(-10**6,10**6)
            return ([f"push i64 {c}"], s64(c))
        n=rnd.choice(list(env)); return ([f"push i64 {n}"], env[n])
    k=rnd.choice(['add','sub','mul','neg','call','t8','t32','add','sub','mul'])
    if k in('add','sub','mul'):
        a,av=gen(d-1,env,rnd); b,bv=gen(d-1,env,rnd)
        v=av+bv if k=='add' else av-bv if k=='sub' else av*bv
        return (a+b+[f"{k} i64"], s64(v))
    if k=='neg':
        a,av=gen(d-1,env,rnd); return (a+["neg i64"], s64(-av))
    if k=='t8':
        a,av=gen(d-1,env,rnd); return (a+["trunc u8","zext i64"], wrap(av,8,False))
    if k=='t32':
        a,av=gen(d-1,env,rnd); return (a+["trunc i32","sext i64"], wrap(av,32,True))
    x,xv=gen(d-1,env,rnd); y,yv=gen(d-1,env,rnd); z,zv=gen(d-1,env,rnd)
    return (x+y+z+["call f()"], s64(xv*3+yv-zv))
def program(seed):
    rnd=random.Random(seed)
    env={n:s64(rnd.choice(CONSTS) if rnd.random()<.6 else rnd.randint(-10**9,10**9)) for n in "abcd"}
    code,val=gen(rnd.randint(2,6),env,rnd)
    L=["section .text:","define global i64 f(i64 x, i64 y, i64 z):","entry:",
       "  push i64 x","  push i64 3","  * i64","  push i64 y","  + i64","  push i64 z","  - i64","  ret","end","end",
       "define global i32 _start:"]
    L+=[f"  local i64 {n} = {v}" for n,v in env.items()]
    L+=["entry:"]+["  "+l for l in code]+["  trunc i32","  syscall exit","end","end","end"]
    return "\n".join(L), wrap(val,8,False)
N=int(sys.argv[1]); bad=0
for seed in range(N):
    src,want=program(seed)
    open('/tmp/fz_p.sl','w').write(src)
    res={}
    for o in ('-O0','-O1'):
        r=subprocess.run(['./silicon','/tmp/fz_p.sl','-o','/tmp/fz_p',o],capture_output=True)
        if r.returncode: res[o]=('COMPILE-ERROR',r.stderr.decode().strip()); continue
        res[o]=subprocess.run(['/tmp/fz_p']).returncode
    if not (res['-O0']==want and res['-O1']==want):
        bad+=1; open(f'/tmp/fz_fail_{seed}.sl','w').write(src)
        if bad<=3: print("MISMATCH seed",seed,"want",want,"got",res)
print(f"{N} random programs: {N-bad} agree with the Python reference at both -O0 and -O1, {bad} mismatches")
