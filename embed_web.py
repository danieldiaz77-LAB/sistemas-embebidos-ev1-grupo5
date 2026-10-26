"""Run after editing dashboard.html; emits a dependency-free C asset."""
from pathlib import Path
root = Path(__file__).resolve().parent
raw = (root / 'dashboard.html').read_bytes()
lines = [','.join(str(v) for v in raw[i:i+24]) for i in range(0, len(raw), 24)]
(root / 'src/web_page.h').write_text(
    '#ifndef WEB_PAGE_H\n#define WEB_PAGE_H\n'
    'static const unsigned char WEB_PAGE[] = {\n' + ',\n'.join(lines) +
    '\n};\n#define WEB_PAGE_LEN (sizeof(WEB_PAGE))\n#endif\n')
print(f'Embedded {len(raw)} bytes')
