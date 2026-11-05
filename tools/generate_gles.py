"""Generate typed cdecl adapters from the installed Khronos declarations."""
import argparse
from pathlib import Path
import re

CUSTOM = {
    'glActiveTexture', 'glBindTexture', 'glTexImage2D', 'glBindRenderbuffer',
    'glRenderbufferStorage', 'glCheckFramebufferStatus', 'glViewport',
    'glBindFramebuffer', 'glDeleteFramebuffers', 'glFramebufferRenderbuffer',
    'glFramebufferTexture2D', 'glGetIntegerv', 'glGetFloatv', 'glGetString',
    'glGetError', 'glMapBufferOES', 'glUnmapBufferOES',
    'glCompressedTexImage2D', 'glCompressedTexSubImage2D',
}
CORE = {name: name.removesuffix('OES') for name in (
    'glTexImage3DOES', 'glTexSubImage3DOES',
    'glCompressedTexImage3DOES', 'glCompressedTexSubImage3DOES')}

def generate(include, inventory):
    declarations = {}
    for header in ('GLES2/gl2.h', 'GLES2/gl2ext.h', 'GLES3/gl3.h'):
        text = (include / header).read_text(encoding='utf-8')
        for ret, name, args in re.findall(
                r'GL_APICALL\s+([^;\n]+?)\s*GL_APIENTRY\s+(gl\w+)\s*\(([^;]+)\);', text):
            declarations[name] = (ret.strip(), args.strip())
    names = [n.strip() for n in inventory.read_text().splitlines() if n.startswith('gl')]
    assert len(names) == len(set(names))
    lines = ['// Generated from Khronos headers and assets/manifests/x86_imports.txt.']
    for name in names:
        ret, args = declarations[name]
        parameters = [] if args == 'void' else args.split(',')
        argnames = [re.search(r'(\w+)\s*$', p).group(1) for p in parameters]
        kind = 'CUSTOM' if name in CUSTOM else 'FORWARD'
        lines.append(f'NOVA_GL_{kind}({ret}, {name}, {len(parameters)}, '
                     f'({args}), ({", ".join(argnames)}), {CORE.get(name, name)})')
    return '\n'.join(lines) + '\n'

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--include', type=Path, required=True)
    p.add_argument('--inventory', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.parent.mkdir(parents=True, exist_ok=True)
    text = generate(a.include, a.inventory)
    if not a.output.exists() or a.output.read_text() != text:
        a.output.write_text(text, encoding='utf-8')
