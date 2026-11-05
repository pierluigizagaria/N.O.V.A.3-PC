"""Attest the opt-in input before the bounded native-body/pbuffer fixture."""
from pathlib import Path
import hashlib
import subprocess
import sys

source=Path(sys.argv[2])
if not source.exists():
    print('SKIP: real library is absent')
    sys.exit(77)
with source.open('rb') as stream:
    actual=hashlib.file_digest(stream,'sha256').hexdigest()
if actual!='a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e':
    raise SystemExit('Native image identity mismatch')
if len(sys.argv)>3:
    result=subprocess.run([sys.argv[1],str(source),'--native1280-negative'],check=False,capture_output=True,text=True)
    print(result.stdout+result.stderr,end='')
    if result.returncode!=1 or 'actual native first resize publishes the actual creation extent' not in result.stderr:
        raise SystemExit('Native1280 omission control did not fail the required extent assertion')
    print('PASS: fixed1280 negative rejected at native creation extent')
else:
    sys.exit(subprocess.run([sys.argv[1],str(source)],check=False).returncode)
