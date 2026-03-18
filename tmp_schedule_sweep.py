from pathlib import Path
import subprocess, re, textwrap, json
repo = Path('/home/dvorak/slambench')
cpp = repo / 'frontend' / 'frontend_halide_harris_lk.cpp'
orig = cpp.read_text()
orig_manual = orig.replace('const bool use_autoschedule = true;', 'const bool use_autoschedule = false;')
pattern = re.compile(r"else \{\n(?:.|\n)*?\n  \}\n\n  return out\.realize", re.M)
variants = {
    'manual_baseline_t32_v8': textwrap.dedent('''
      else {
        Var yo("yo"), yi("yi");
        out.split(y, yo, yi, 32).parallel(yo).vectorize(x, 8);
        cov_xx_h.compute_at(out, yo).vectorize(x, 8);
        cov_xy_h.compute_at(out, yo).vectorize(x, 8);
        cov_yy_h.compute_at(out, yo).vectorize(x, 8);
        sum_xx.compute_at(out, yi).vectorize(x, 8);
        sum_xy.compute_at(out, yi).vectorize(x, 8);
        sum_yy.compute_at(out, yi).vectorize(x, 8);
        cov_xx.compute_at(out, yi).vectorize(x, 8);
        cov_xy.compute_at(out, yi).vectorize(x, 8);
        cov_yy.compute_at(out, yi).vectorize(x, 8);
        dx.compute_at(out, yi).vectorize(x, 8);
        dy.compute_at(out, yi).vectorize(x, 8);
      }

      return out.realize'''),
    'manual_t16_v8': textwrap.dedent('''
      else {
        Var yo("yo"), yi("yi");
        out.split(y, yo, yi, 16).parallel(yo).vectorize(x, 8);
        cov_xx_h.compute_at(out, yo).vectorize(x, 8);
        cov_xy_h.compute_at(out, yo).vectorize(x, 8);
        cov_yy_h.compute_at(out, yo).vectorize(x, 8);
        sum_xx.compute_at(out, yi).vectorize(x, 8);
        sum_xy.compute_at(out, yi).vectorize(x, 8);
        sum_yy.compute_at(out, yi).vectorize(x, 8);
        cov_xx.compute_at(out, yi).vectorize(x, 8);
        cov_xy.compute_at(out, yi).vectorize(x, 8);
        cov_yy.compute_at(out, yi).vectorize(x, 8);
        dx.compute_at(out, yi).vectorize(x, 8);
        dy.compute_at(out, yi).vectorize(x, 8);
      }

      return out.realize'''),
    'manual_t32_v16': textwrap.dedent('''
      else {
        Var yo("yo"), yi("yi");
        out.split(y, yo, yi, 32).parallel(yo).vectorize(x, 16);
        cov_xx_h.compute_at(out, yo).vectorize(x, 16);
        cov_xy_h.compute_at(out, yo).vectorize(x, 16);
        cov_yy_h.compute_at(out, yo).vectorize(x, 16);
        sum_xx.compute_at(out, yi).vectorize(x, 16);
        sum_xy.compute_at(out, yi).vectorize(x, 16);
        sum_yy.compute_at(out, yi).vectorize(x, 16);
        cov_xx.compute_at(out, yi).vectorize(x, 16);
        cov_xy.compute_at(out, yi).vectorize(x, 16);
        cov_yy.compute_at(out, yi).vectorize(x, 16);
        dx.compute_at(out, yi).vectorize(x, 16);
        dy.compute_at(out, yi).vectorize(x, 16);
      }

      return out.realize'''),
    'manual_store_t32_v8': textwrap.dedent('''
      else {
        Var yo("yo"), yi("yi");
        out.split(y, yo, yi, 32).parallel(yo).vectorize(x, 8);
        cov_xx_h.store_at(out, yo).compute_at(out, yi).vectorize(x, 8);
        cov_xy_h.store_at(out, yo).compute_at(out, yi).vectorize(x, 8);
        cov_yy_h.store_at(out, yo).compute_at(out, yi).vectorize(x, 8);
        sum_xx.compute_at(out, yi).vectorize(x, 8);
        sum_xy.compute_at(out, yi).vectorize(x, 8);
        sum_yy.compute_at(out, yi).vectorize(x, 8);
        cov_xx.compute_at(out, yi).vectorize(x, 8);
        cov_xy.compute_at(out, yi).vectorize(x, 8);
        cov_yy.compute_at(out, yi).vectorize(x, 8);
        dx.compute_at(out, yi).vectorize(x, 8);
        dy.compute_at(out, yi).vectorize(x, 8);
      }

      return out.realize'''),
}
results=[]
for name, repl in variants.items():
    txt = pattern.sub(repl, orig_manual)
    cpp.write_text(txt)
    cmd = 'cd /home/dvorak/slambench && docker run --rm --cpuset-cpus="0,2" -v "$PWD":/workspace --entrypoint bash slambench-local:dev -lc "bash /workspace/run_frontend.sh"'
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    out = p.stdout + '\n' + p.stderr
    m = re.search(r'halide_response_ms:\s*([0-9.]+)', out)
    total = re.search(r'total_ms:\s*([0-9.]+)', out)
    results.append({'name':name, 'code':p.returncode, 'halide_response_ms': m.group(1) if m else None, 'total_ms': total.group(1) if total else None})
cpp.write_text(orig)
print(json.dumps(results, indent=2))
