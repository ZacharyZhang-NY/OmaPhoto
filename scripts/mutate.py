#!/usr/bin/env python3
"""Mutation runner for OmaPhoto. Run from the repository root.

  mutate.py FILE [--targets T1,T2] <<< 'old ==> new' blocks separated by lines of @@
  mutate.py FILE --delete [--from N --to M] [--targets T1,T2]   delete each statement line in turn

Verdicts: 'caught' means the mutant built and a test that really ran failed; 'UNBUILDABLE' means the
compiler rejected it, which proves nothing; 'SURVIVED' means every test passed. Anything else (Docker
not starting, a missing test executable, a failing baseline) stops the run with an error.
"""
import argparse, os, re, subprocess, sys

parser = argparse.ArgumentParser()
parser.add_argument('path')
parser.add_argument('--targets', default='')
parser.add_argument('--delete', action='store_true')
parser.add_argument('--from', dest='first', type=int, default=1)
parser.add_argument('--to', dest='last', type=int, default=10**9)
args = parser.parse_args()

repo = os.getcwd()
if not os.path.isfile(os.path.join(repo, 'CMakeLists.txt')):
    sys.exit('run mutate.py from the repository root')
original = open(args.path).read()
targets = [t for t in args.targets.split(',') if t]


def docker(command):
    uid, gid = os.getuid(), os.getgid()
    runtime = '/run/user/%d' % uid
    done = subprocess.run(
        ['docker', 'run', '--rm', '--init', '--cpus=32', '-u', '%d:%d' % (uid, gid), '-e', 'HOME=/tmp', '-e', 'QT_QPA_PLATFORM=offscreen',
         '--tmpfs', '%s:uid=%d,gid=%d,mode=0700' % (runtime, uid, gid), '-e', 'XDG_RUNTIME_DIR=' + runtime,
         '-v', '%s:%s' % (repo, repo), '-w', repo, 'omaphoto-dev', 'timeout', '600', 'sh', '-c', command],
        capture_output=True, text=True)
    return done.returncode, done.stdout + done.stderr


def build():
    wanted = ' --target ' + ' '.join(targets) if targets else ''
    return docker('cmake -S . -B build -G Ninja -DOMAPHOTO_WERROR=ON >/dev/null && cmake --build build -j32' + wanted)


def test():
    if targets:
        missing = [t for t in targets if not os.path.isfile(os.path.join(repo, 'build', t))]
        if missing:
            sys.exit('test executables are missing after a successful build: %s' % missing)
        return docker(' && '.join('./build/%s' % t for t in targets))
    return docker('ctest --test-dir build --output-on-failure --timeout 120')


def judge():
    code, out = build()
    if code != 0:
        if 'error:' in out or 'FAILED:' in out:
            return 'UNBUILDABLE', next((l for l in out.split('\n') if 'error:' in l), '')
        sys.exit('the build did not run:\n' + out[-2000:])
    code, out = test()
    if code == 0:
        return 'SURVIVED', ''
    if 'Start testing of' not in out and 'Timeout' not in out:
        sys.exit('the tests did not run:\n' + out[-2000:])
    return 'caught', next((l for l in out.split('\n') if l.startswith('FAIL!') or 'Timeout' in l or 'Segmentation' in l), 'test process died')


mutants = []
if args.delete:
    skipped = ('const ', 'static ', '#', '//', 'using ', 'namespace ', 'struct ', 'class ', '}', 'return', 'break', 'continue')
    lines = original.split('\n')
    for number, line in enumerate(lines, 1):
        stripped = line.strip()
        if not (args.first <= number <= args.last) or not stripped.endswith(';') or stripped.startswith(skipped):
            continue
        mutated = lines[:]
        mutated[number - 1] = line[:len(line) - len(line.lstrip())] + '(void)0;'
        mutants.append(('L%d %s' % (number, stripped), '\n'.join(mutated)))
else:
    for block in sys.stdin.read().split('\n@@\n'):
        block = block.strip('\n')
        if not block:
            continue
        old, new = block.split(' ==> ')
        if original.count(old) != 1:
            sys.exit('the text to mutate must occur exactly once: %r occurs %d times' % (old, original.count(old)))
        mutants.append((old.replace('\n', ' '), original.replace(old, new)))

verdict, reason = judge()
if verdict != 'SURVIVED':
    sys.exit('the unmutated baseline is not green (%s): %s' % (verdict, reason))

counts = {'caught': 0, 'SURVIVED': 0, 'UNBUILDABLE': 0}
try:
    for label, text in mutants:
        open(args.path, 'w').write(text)
        verdict, reason = judge()
        counts[verdict] += 1
        print('%-11s | %-70s | %s' % (verdict, label[:70], reason[:100]))
        sys.stdout.flush()
finally:
    open(args.path, 'w').write(original)
print('mutants: %d  caught: %d  survivors: %d  unbuildable: %d' % (len(mutants), counts['caught'], counts['SURVIVED'], counts['UNBUILDABLE']))
