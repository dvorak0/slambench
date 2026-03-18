from pathlib import Path
import subprocess, re, json
repo = Path('/home/dvorak/slambench')
cpp = repo / 'frontend' / 'frontend_halide_harris_lk.cpp'
orig = cpp.read_text()
variants = {
    'sched_t16_v8': ('const int tile_size = 32;', 'const int tile_size = 16;'),
    'sched_t32_v8': ('const int tile_size = 32;', 'const int tile_size = 32;'),
    'sched_t64_v8': ('const int tile_size = 32;', 'const int tile_size = 64;'),
    'sched_t32_v16': [('const int vec = 8;', 'const int vec = 16;'), ('const int tile_size = 32;', 'const int tile_size = 32;')],
}
results=[]
for name, repl in variants.items():
    txt = orig
    if isinstance(repl, tuple):
        txt = txt.replace(repl[0], repl[1], 1)
    else:
        for a,b in repl:
            txt = txt.replace(a,b,1)
    cpp.write_text(txt)
    cmd = 'cd /home/dvorak/slambench && docker run --rm --cpuset-cpus="0,2" -v "$PWD":/workspace --entrypoint bash slambench-local:dev -lc "bash /workspace/run_frontend.sh | tail -220"'
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    out = p.stdout + '\n' + p.stderr
    m = re.search(r'halide_response_ms\s*\|\s*([0-9.]+)', out)
    if not m:
        m = re.search(r'halide_response_ms:\s*([0-9.]+)', out)
    t = re.search(r'total_ms\s*\|\s*([0-9.]+)', out)
    if not t:
        t = re.search(r'total_ms:\s*([0-9.]+)', out)
    results.append({'name': name, 'code': p.returncode, 'halide_response_ms': float(m.group(1)) if m else None, 'total_ms': float(t.group(1)) if t else None})
cpp.write_text(orig)
print(json.dumps(results, indent=2))
