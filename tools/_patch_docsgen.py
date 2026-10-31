from pathlib import Path
p = Path("tools/build_docs_site.py")
s = p.read_text(encoding="utf-8")

s = s.replace("""    index = 0
    for path, group, fallback_title in entries:""",
"""    doc_ids = {p.resolve(): slug_id(i) for i, (p, _g, _t) in enumerate(entries)}

    index = 0
    for path, group, fallback_title in entries:""")

old = '''        body = re.sub(r'src="([^"]+)"', repl, body)'''
new = '''        body = re.sub(r'src="([^"]+)"', repl, body)

        # 把 markdown 之间的 .md 链接改成锚点，保证点击能跳章
        def link_repl(m):
            target = (path.parent / m.group(1)).resolve()
            doc_id = doc_ids.get(target)
            return f'href="#{doc_id}"' if doc_id else m.group(0)

        body = re.sub(r'href="([^"]+\\.md)(#[^"]*)?"', link_repl, body)'''
assert old in s
s = s.replace(old, new)
p.write_text(s, encoding="utf-8")
print("patched")
