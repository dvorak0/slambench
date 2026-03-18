from pathlib import Path
import subprocess, re, json
repo = Path('/home/dvorak/slambench')
cpp = repo / 'frontend' / 'frontend_halide_harris_lk.cpp'
# old structure before OpenCV-like rewrite
old_src = subprocess.check_output(
    ['git', '-C', str(repo), 'show', '73f6ae2:frontend/frontend_halide_harris_lk.cpp'],
    text=True,
)
old_src = old_src.replace('const bool use_autoschedule = true;', 'const bool use_autoschedule = false;')
variants = {
    'old_t8_v8': '''  } else {
    Var yo("yo"), yi("yi");
    out.split(y, yo, yi, 8).parallel(yo).vectorize(x, 8);
    in_f.compute_at(out, yi).vectorize(x, 8);
    Ix.compute_at(out, yi).vectorize(x, 8);
    Iy.compute_at(out, yi).vectorize(x, 8);
    Ixx.compute_at(out, yi).vectorize(x, 8);
    Iyy.compute_at(out, yi).vectorize(x, 8);
    Ixy.compute_at(out, yi).vectorize(x, 8);
    Sxx.compute_at(out, yi).vectorize(x, 8);
    Syy.compute_at(out, yi).vectorize(x, 8);
    Sxy.compute_at(out, yi).vectorize(x, 8);
    det.compute_at(out, yi).vectorize(x, 8);
    trace.compute_at(out, yi).vectorize(x, 8);
  }
''',
    'old_t16_v8': '''  } else {
    Var yo("yo"), yi("yi");
    out.split(y, yo, yi, 16).parallel(yo).vectorize(x, 8);
    in_f.compute_at(out, yi).vectorize(x, 8);
    Ix.compute_at(out, yi).vectorize(x, 8);
    Iy.compute_at(out, yi).vectorize(x, 8);
    Ixx.compute_at(out, yi).vectorize(x, 8);
    Iyy.compute_at(out, yi).vectorize(x, 8);
    Ixy.compute_at(out, yi).vectorize(x, 8);
    Sxx.compute_at(out, yi).vectorize(x, 8);
    Syy.compute_at(out, yi).vectorize(x, 8);
    Sxy.compute_at(out, yi).vectorize(x, 8);
    det.compute_at(out, yi).vectorize(x, 8);
    trace.compute_at(out, yi).vectorize(x, 8);
  }
''',
    'old_t32_v8': '''  } else {
    Var yo("yo"), yi("yi");
    out.split(y, yo, yi, 32).parallel(yo).vectorize(x, 8);
    in_f.compute_at(out, yi).vectorize(x, 8);
    Ix.compute_at(out, yi).vectorize(x, 8);
    Iy.compute_at(out, yi).vectorize(x, 8);
    Ixx.compute_at(out, yi).vectorize(x, 8);
    Iyy.compute_at(out, yi).vectorize(x, 8);
    Ixy.compute_at(out, yi).vectorize(x, 8);
    Sxx.compute_at(out, yi).vectorize(x, 8);
    Syy.compute_at(out, yi).vectorize(x, 8);
    Sxy.compute_at(out, yi).vectorize(x, 8);
    det.compute_at(out, yi).vectorize(x, 8);
    trace.compute_at(out, yi).vectorize(x, 8);
  }
''',
    'old_t64_v8': '''  } else {
    Var yo("yo"), yi("yi");
    out.split(y, yo, yi, 64).parallel(yo).vectorize(x, 8);
    in_f.compute_at(out, yi).vectorize(x, 8);
    Ix.compute_at(out, yi).vectorize(x, 8);
    Iy.compute_at(out, yi).vectorize(x, 8);
    Ixx.compute_at(out, yi).vectorize(x, 8);
    Iyy.compute_at(out, yi).vectorize(x, 8);
    Ixy.compute_at(out, yi).vectorize(x, 8);
    Sxx.compute_at(out, yi).vectorize(x, 8);
    Syy.compute_at(out, yi).vectorize(x, 8);
    Sxy.compute_at(out, yi).vectorize(x, 8);
    det.compute_at(out, yi).vectorize(x, 8);
    trace.compute_at(out, yi).vectorize(x, 8);
  }
''',
}
results = []
orig = cpp.read_text()
for name, sched in variants.items():
    src = re.sub(r'  \} else \{\n(?:.|\n)*?  \}\n\n  return out\.realize', sched + '\n  return out.realize', old_src, count=1)
    cpp.write_text(src)
    cmd = 'cd /home/dvorak/slambench && docker run --rm --cpuset-cpus="0,2" -v "$PWD":/workspace --entrypoint bash slambench-local:dev -lc "bash /workspace/run_frontend.sh | tail -220"'
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    out = p.stdout + '\n' + p.stderr
    m = re.search(r'halide_response_ms\s*\|\s*([0-9.]+)', out)
    if not m:
        m = re.search(r'halide_response_ms:\s*([0-9.]+)', out)
    t = re.search(r'total_ms\s*\|\s*([0-9.]+)', out)
    if not t:
        t = re.search(r'total_ms:\s*([0-9.]+)', out)
    results.append({
        'name': name,
        'code': p.returncode,
        'halide_response_ms': float(m.group(1)) if m else None,
        'total_ms': float(t.group(1)) if t else None,
    })
cpp.write_text(orig)
print(json.dumps(results, indent=2))
