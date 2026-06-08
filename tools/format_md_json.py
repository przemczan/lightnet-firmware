import json
import re
from pathlib import Path

root = Path(r'D:\Projects\Lightnet\lightnet-firmware')
pattern = re.compile(r'```json\n(.*?)\n```', re.DOTALL)
changed_files = []
errors = []

for path in root.rglob('*.md'):
    text = path.read_text(encoding='utf-8')
    def repl(match):
        block = match.group(1)
        try:
            data = json.loads(block)
        except Exception as exc:
            try:
                data = json.loads(block.strip())
            except Exception:
                errors.append((str(path), str(exc), block[:120].replace('\n', ' ')))
                return match.group(0)
        pretty = json.dumps(data, indent=2, ensure_ascii=False)
        return '```json\n' + pretty + '\n```'
    new_text = pattern.sub(repl, text)
    if new_text != text:
        path.write_text(new_text, encoding='utf-8')
        changed_files.append(str(path))

print(f'changed files: {len(changed_files)}')
for p in changed_files:
    print('CHANGED', p)
print(f'errors: {len(errors)}')
for path, err, snippet in errors[:20]:
    print('ERROR', path, err, snippet)
if errors:
    raise SystemExit(1)
