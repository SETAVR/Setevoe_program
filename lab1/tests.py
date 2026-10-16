"""Behavioral checks; works with Linux binary or portable Cygwin executable."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys

exe = str(Path(sys.argv[1] if len(sys.argv) > 1 else './lab1').resolve())
records = []

def check(name, args, expected, tokens, prepare=None):
    options = {'preexec_fn': prepare} if prepare else {}
    p = subprocess.run([exe, *args], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, **options)
    passed = p.returncode == expected and all(t in p.stdout for t in tokens)
    records.append(dict(name=name, args=args, exit=p.returncode,
                        passed=passed, output=p.stdout))
    print(('PASS' if passed else 'FAIL') + ': ' + name)
    if not passed: print(p.stdout)

check('basic variant 6', [], 0,
      ['192.168.1.1:67', 'network bytes=00 43', 'fd >= 3: PASS',
       'Released descriptor check: EBADF: PASS'])
check('medium variant 6', ['--medium'], 0,
      ['Bound address: 0.0.0.0:5000', 'network bytes=13 88', 'close('])
check('advanced variant 6', ['AF_INET','SOCK_DGRAM','0.0.0.0','5000','--bind'],
      0, ['Bound address: 0.0.0.0:5000', 'fd >= 3: PASS'])
check('invalid IP', ['AF_INET','SOCK_DGRAM','999.1.1.1','67'], 1,
      ['inet_pton=0'])
check('port overflow', ['AF_INET','SOCK_DGRAM','127.0.0.1','65536'], 1,
      ['Invalid port'])
check('invalid socket type', ['AF_INET','SOCK_RAW','127.0.0.1','5000'], 1,
      ['Invalid socket type'])
check('invalid domain', ['AF_UNIX','SOCK_DGRAM','127.0.0.1','5000'], 1,
      ['Invalid domain'])
for port in ['-1', 'abc', '12x', '', '999999999999999999999999999999']:
    check('invalid port ' + repr(port),
          ['AF_INET','SOCK_DGRAM','127.0.0.1',port], 1, ['Invalid port'])
check('port zero', ['AF_INET','SOCK_DGRAM','127.0.0.1','0','--bind'], 0,
      ['Bound address: 127.0.0.1:', 'Released descriptor check: EBADF: PASS'])
assert not records[-1]['output'].split('Bound address: ')[-1].startswith('127.0.0.1:0\n')
check('port upper bound', ['AF_INET','SOCK_DGRAM','127.0.0.1','65535'], 0,
      ['network bytes=FF FF'])
check('TCP supported', ['AF_INET','SOCK_STREAM','127.0.0.1','0','--bind'], 0,
      ['type=SOCK_STREAM', 'Bound address: 127.0.0.1:'])
check('unknown option', ['AF_INET','SOCK_DGRAM','127.0.0.1','0','--bad'], 1,
      ['Unknown option'])
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as busy:
    if os.name == 'nt':
        busy.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
    busy.bind(('127.0.0.1', 0))
    port = str(busy.getsockname()[1])
    check('occupied port', ['AF_INET','SOCK_DGRAM','127.0.0.1',port,'--bind'],
          1, ['EADDRINUSE', 'close(', 'Released descriptor check: EBADF: PASS'])
check('port reusable after close', ['AF_INET','SOCK_DGRAM','127.0.0.1',port,'--bind'],
      0, ['Bound address: 127.0.0.1:' + port])
check('help', ['--help'], 0, ['Usage:', '--bind'])
if os.name == 'posix':
    def close_stdin():
        os.close(0)
    check('closed stdin still fd >= 3', [], 0, ['fd >= 3: PASS'], close_stdin)
elif os.environ.get('LAB1_BASH'):
    posix_exe = '/cygdrive/' + exe[0].lower() + exe[2:].replace('\\', '/')
    for name, redirection, expected_text in [
        ('closed stdin still fd >= 3', '0<&-', 'fd >= 3: PASS'),
        ('all standard descriptors closed', '0<&- 1>&- 2>&-', '')
    ]:
        p = subprocess.run([os.environ['LAB1_BASH'], '-c',
                            'exec ' + redirection + '; exec "$1"',
                            'lab1-test', posix_exe],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        passed = p.returncode == 0 and expected_text in p.stdout
        records.append(dict(name=name, args=[redirection], exit=p.returncode,
                            passed=passed, output=p.stdout))
        print(('PASS' if passed else 'FAIL') + ': ' + name)
Path('test_results.json').write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding='utf-8')
print(f'{sum(r["passed"] for r in records)}/{len(records)} checks passed')
sys.exit(0 if all(r['passed'] for r in records) else 1)
