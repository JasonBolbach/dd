#!/usr/bin/env python3
"""Generates the API reference wiki pages (docs/wiki/API-*.md) from the /** ... */ doc comments in src/*.h.

    python3 tools/gen_docs.py            # rewrite docs/wiki/API-*.md, API-Index.md and _Sidebar.md
    python3 tools/gen_docs.py --check    # also list public declarations that have no doc comment

The doc comment format is Doxygen-like: a brief first paragraph, then optional free text, @param name text,
@tparam, @return, @warning (pitfalls), @note (tips), @par Title (a titled section), @code ... @endcode, @file (page intro).
The hand-written pages (Home, Getting-Started, Concepts, ...) are not touched.
"""
import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC = os.path.join(ROOT, "src")
OUT = os.path.join(ROOT, "docs", "wiki")

# header -> (page name, one-line summary)  (order = sidebar order)
HEADERS = [
    ("ddPoint.h",    "Points, matrices and transforms"),
    ("ddArena.h",    "Arena allocator"),
    ("ddGeom.h",     "Collision and measurement maths"),
    ("ddShapeSet.h", "Collision geometry for shapes"),
    ("ddRender.h",   "Renderer, pens, polygon fill"),
    ("ddVector.h",   "Scene and primitives"),
    ("ddCollide.h",  "Shape-to-shape collision"),
    ("ddInteract.h", "Touch / mouse interaction"),
    ("ddSDLInput.h", "Desktop mouse mapping"),
]

# ------------------------------------------------------------------ parsing
def parse_doc(raw):
    """raw: list of comment text lines (without the leading ' * '). Returns a dict."""
    text = []
    for l in raw:
        # split inline repeated @param onto their own lines
        parts = re.split(r'\s(?=@param\s)', l)
        text.extend(parts)
    d = {"brief": "", "body": [], "params": [], "returns": [], "warnings": [], "notes": [], "code": [], "pars": [], "file": False, "tparams": []}
    cur = None   # (kind, buffer list)
    in_code = False
    code = []
    def flush():
        nonlocal cur
        if cur:
            kind, buf = cur
            s = "\n".join(buf).strip()
            if kind == "param":
                m = re.match(r'(\S+)\s*(.*)', s, re.S)
                d["params"].append((m.group(1), m.group(2).strip()) if m else (s, ""))
            elif kind == "tparam":
                m = re.match(r'(\S+)\s*(.*)', s, re.S)
                d["tparams"].append((m.group(1), m.group(2).strip()) if m else (s, ""))
            elif kind == "return": d["returns"].append(s)
            elif kind == "warning": d["warnings"].append(s)
            elif kind == "note": d["notes"].append(s)
            elif kind == "par":
                title, _, rest = s.partition("\n")
                d["pars"].append((title.strip(), rest))
            elif kind == "body": d["body"].append(s)
        cur = None
    for l in text:
        st = l.strip()
        if in_code:
            if st.startswith("@endcode"):
                in_code = False
                d["code"].append("\n".join(code)); code = []
                cur = None
            else:
                code.append(l.rstrip())
            continue
        if st.startswith("@code"):
            flush(); in_code = True; code = []; continue
        m = re.match(r'@(file|param|tparam|return|returns|warning|note|par)\b\s*(.*)', st)
        if m:
            flush()
            kind = m.group(1)
            if kind == "returns": kind = "return"
            if kind == "file":
                d["file"] = True
                cur = ("body", [])   # file name line is dropped below
                continue
            cur = (kind, [m.group(2)])
            continue
        if cur is None:
            cur = ("body", [l.rstrip()])
        else:
            cur[1].append(l.rstrip())
    flush()
    # first body paragraph (or first line) is the brief
    body = [b for b in d["body"] if b.strip()]
    if d["file"] and body:
        # first line of the @file body is the file name
        first, _, rest = body[0].partition("\n")
        body[0] = rest.strip("\n") if rest.strip() else ""
        body = [b for b in body if b.strip()]
    d["body"] = body
    return d

def clean_signature(lines):
    sig = " ".join(l.strip() for l in lines)
    sig = re.sub(r'\s+', ' ', sig)
    # cut at body / initializer list
    depth = 0; out = ""
    for i, ch in enumerate(sig):
        if ch == '(': depth += 1
        elif ch == ')': depth -= 1
        if depth == 0 and ch in "{;":
            break
        if depth == 0 and ch == ':' and sig[i-1:i+1] != '::' and sig[i:i+2] != '::' and out.rstrip().endswith(')') and not re.search(r'(public|private|protected)\s*$', out):
            # constructor initializer list
            break
        out += ch
    out = out.strip()
    out = re.sub(r'\s*=\s*default$', ' = default', out)
    return out

def sig_kind_name(sig):
    s = sig
    m = re.match(r'(?:template\s*<[^>]*>\s*)?(struct|class)\s+(\w+)', s)
    if m: return m.group(1), m.group(2)
    m = re.match(r'enum\s+(?:struct\s+)?(\w+)', s)
    if m: return "enum", m.group(1)
    m = re.match(r'typedef .*\(\*(\w+)\)', s)
    if m: return "type", m.group(1)
    m = re.match(r'#define\s+(\w+)', s)
    if m: return "macro", m.group(1)
    m = re.search(r'(operator\s*[^\s(]+|~?\w+)\s*\(', s)
    if m and not s.startswith("using"):
        return "function", m.group(1)
    m = re.match(r'(?:static\s+|constexpr\s+|inline\s+|const\s+)*[\w:<>,\*&\s]+?\s+[\*&]*(\w+)(?:\[[^\]]*\])?(?:\s*=.*)?$', s)
    if m: return "field", m.group(1)
    return "other", s[:40]

def load(header):
    path = os.path.join(SRC, header)
    lines = open(path).read().split("\n")
    entries = []            # dicts
    current_class = None
    undocumented = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if re.match(r'^(struct|class)\s+\w+[^;]*\{\s*$', line) or re.match(r'^(struct|class)\s+\w+\s*:\s*[^;{]*\{?\s*$', line):
            m = re.match(r'^(struct|class)\s+(\w+)', line)
            current_class = m.group(2)
        if re.match(r'^\};', line):
            current_class = None
        if line.strip().startswith("/**"):
            raw = []
            j = i
            first = lines[j].strip()[3:]
            if "*/" in first:                       # one-line /** ... */
                raw.append(first.split("*/")[0].strip()); end = j
            else:
                if first.strip(): raw.append(first.strip())
                j += 1
                while "*/" not in lines[j]:
                    t = lines[j].strip()
                    t = t[1:] if t.startswith("*") else t
                    raw.append(t[1:] if t.startswith(" ") else t)
                    j += 1
                end = j
            doc = parse_doc(raw)
            k = end + 1
            sigl = []
            while k < len(lines) and not lines[k].strip(): k += 1
            if not doc["file"]:
                if k < len(lines):
                    first = lines[k]
                    sigl.append(first)
                    if first.strip().startswith("template"):
                        sigl.append(lines[k + 1])
                        k += 1
                    # join continuation lines while parentheses are unbalanced
                    joined = " ".join(sigl)
                    n = 0
                    while joined.count("(") > joined.count(")") and n < 3:
                        k += 1; sigl.append(lines[k]); joined = " ".join(sigl); n += 1
                    if first.strip().startswith("#ifndef"):
                        sigl = ["#define " + first.strip().split()[1]]
                sig = clean_signature(sigl)
                kind, name = sig_kind_name(sig)
                indent = len(lines[end + 1 + 0]) - len(lines[end + 1].lstrip()) if end + 1 < len(lines) else 0
                top = not lines[k - len(sigl) + 1].startswith(" ") if sigl else True
                entries.append({"doc": doc, "sig": sig, "kind": kind, "name": name,
                                "cls": None if top else current_class, "line": i + 1})
            else:
                entries.append({"doc": doc, "file": True, "sig": "", "kind": "file", "name": header, "cls": None, "line": i + 1})
            i = end + 1
            continue
        i += 1
    return entries

# ------------------------------------------------------------------ rendering
def slug(text, seen):
    s = text.lower()
    s = re.sub(r'[^a-z0-9 \-_]', '', s)
    s = s.replace(" ", "-")
    n = seen.get(s, 0); seen[s] = n + 1
    return s if n == 0 else f"{s}-{n}"

def para_blocks(text):
    """text with '- ' lists -> markdown with blank lines around lists; indented continuation lines stay in their bullet."""
    out = []; prev_list = False
    for l in text.split("\n"):
        st = l.strip()
        is_list = bool(re.match(r'(- |\* |\d+\. )', st))
        if is_list:
            if out and not prev_list and out[-1].strip(): out.append("")
            out.append(st)
            prev_list = True
        elif prev_list and st and l.startswith(" "):
            out[-1] = out[-1] + " " + st                 # continuation of the bullet
        else:
            if prev_list and st: out.append("")
            out.append(st)
            prev_list = False
    return "\n".join(out)

def md_body(text, links):
    # unindent and turn "  - x" lists into markdown
    lines = [l for l in text.split("\n")]
    # remove common indentation
    ind = min((len(l) - len(l.lstrip()) for l in lines if l.strip()), default=0)
    lines = [l[ind:] if l.strip() else "" for l in lines]
    s = para_blocks("\n".join(lines))
    return autolink(s, links)

def autolink(s, links):
    parts = re.split(r'(`[^`]*`)', s)
    res = []
    for p in parts:
        if p.startswith("`") and p.endswith("`") and len(p) > 2:
            inner = p[1:-1]
            if inner in links:
                res.append(f"[`{inner}`]({links[inner]})")
            else:
                res.append(p)
        else:
            res.append(re.sub(r'\b(dd[A-Z]\w*|Matrix3x3|RenderBlock|DD_[A-Z_]+|DDD_[A-Z0-9_]+)\b',
                              lambda m: f"[{m.group(1)}]({links[m.group(1)]})" if m.group(1) in links else m.group(1), p))
    return "".join(res)

def render_entry(e, level, links, seen):
    d = e["doc"]; out = []
    title = e["name"]
    if e["kind"] == "function" and e["cls"]:
        title = f'{e["cls"]}::{e["name"]}'
    heading = {"struct": f"struct {e['name']}", "class": f"class {e['name']}", "enum": f"enum {e['name']}"}.get(e["kind"], title)
    if e["kind"] == "function":
        heading = title + "()"
    n = seen.setdefault("#titles", {}).get(heading, 0)
    seen["#titles"][heading] = n + 1
    if n: heading += f" (overload {n + 1})"
    out.append("#" * level + " " + heading)
    anchor = slug(heading, seen)
    out.append("")
    if e["sig"]:
        out.append("```cpp"); out.append(e["sig"]); out.append("```"); out.append("")
    body = d["body"]
    if body:
        first = body[0].strip()
        out.append(md_body(body[0], links)); out.append("")
        for b in body[1:]:
            out.append(md_body(b, links)); out.append("")
    for title_, rest in d["pars"]:
        out.append(f"**{title_}**"); out.append("")
        out.append(md_body(rest, links)); out.append("")
    if d["tparams"] or d["params"]:
        out.append("| Parameter | Description |"); out.append("|---|---|")
        for n, t in d["tparams"] + d["params"]:
            out.append(f"| `{n}` | {autolink(t.replace(chr(10), ' ').replace('|', '\\|'), links)} |")
        out.append("")
    if d["returns"]:
        out.append("**Returns:** " + " ".join(autolink(r.replace("\n", " "), links) for r in d["returns"])); out.append("")
    for c in d["code"]:
        out.append("```cpp"); out.append(c); out.append("```"); out.append("")
    if d["warnings"]:
        out.append("**Pitfalls**"); out.append("")
        for w in d["warnings"]: out.append("- " + autolink(re.sub(r'\s*\n\s*', ' ', w), links))
        out.append("")
    if d["notes"]:
        out.append("**Tips and notes**"); out.append("")
        for n in d["notes"]: out.append("- " + autolink(re.sub(r'\s*\n\s*', ' ', n), links))
        out.append("")
    return "\n".join(out), anchor, heading

def main():
    check = "--check" in sys.argv
    pages = {}
    # first pass: collect symbols for links
    links = {}
    loaded = {}
    for h, _ in HEADERS:
        ents = load(h); loaded[h] = ents
        page = "API-" + h.replace(".h", "")
        seen = {}
        for e in ents:
            if e.get("file"): continue
            if e["kind"] in ("struct", "class", "enum", "macro", "type") or (e["kind"] == "function" and not e["cls"]) or (e["kind"] == "field" and not e["cls"]):
                links.setdefault(e["name"], f"{page}")  # page-level link (anchors added below)
    # compute anchors precisely with a render pass
    anchors = {}
    rendered = {}
    for h, summary in HEADERS:
        page = "API-" + h.replace(".h", "")
        seen = {}
        text = []
        ents = loaded[h]
        intro = [e for e in ents if e.get("file")]
        text.append(f"# {h}")
        text.append("")
        text.append(f"*{summary}.*  Source: [`src/{h}`](https://github.com/JasonBolbach/dd/blob/main/src/{h})")
        text.append("")
        if intro:
            d = intro[0]["doc"]
            for b in d["body"]: text.append(md_body(b, links)); text.append("")
            for t, rest in d["pars"]: text.append(f"**{t}**"); text.append(""); text.append(md_body(rest, links)); text.append("")
            for c in d["code"]: text.append("```cpp"); text.append(c); text.append("```"); text.append("")
            if d["warnings"]:
                text.append("**Pitfalls**"); text.append("")
                for w in d["warnings"]: text.append("- " + autolink(re.sub(r'\s*\n\s*', ' ', w), links))
                text.append("")
            if d["notes"]:
                text.append("**Tips and notes**"); text.append("")
                for n in d["notes"]: text.append("- " + autolink(re.sub(r'\s*\n\s*', ' ', n), links))
                text.append("")
        toc_at = len(text)
        toc = []
        cur_top = None
        for e in ents:
            if e.get("file"): continue
            level = 3 if (e["cls"] and e["kind"] in ("function", "field")) else 2
            md, anchor, heading = render_entry(e, level, links, seen)
            text.append(md); text.append("---" if level == 2 else ""); 
            if level == 2:
                toc.append(f"- [{heading}](#{anchor})")
                if e["kind"] in ("struct", "class", "enum", "macro", "type") or (e["kind"] in ("function", "field") and not e["cls"]):
                    anchors[e["name"]] = f"{page}#{anchor}"
        text[toc_at:toc_at] = ["## Contents", ""] + toc + [""]
        rendered[page] = "\n".join(text)
    # second pass with precise anchors
    for k in list(links): links[k] = anchors.get(k, links[k])
    for h, summary in HEADERS:
        page = "API-" + h.replace(".h", "")
        seen = {}
        text = [f"# {h}", "", f"*{summary}.*  Source: [`src/{h}`](https://github.com/JasonBolbach/dd/blob/main/src/{h})", ""]
        ents = loaded[h]
        intro = [e for e in ents if e.get("file")]
        if intro:
            d = intro[0]["doc"]
            for b in d["body"]: text.append(md_body(b, links)); text.append("")
            for t, rest in d["pars"]: text.append(f"**{t}**"); text.append(""); text.append(md_body(rest, links)); text.append("")
            for c in d["code"]: text.append("```cpp"); text.append(c); text.append("```"); text.append("")
            if d["warnings"]:
                text.append("**Pitfalls**"); text.append("")
                for w in d["warnings"]: text.append("- " + autolink(re.sub(r'\s*\n\s*', ' ', w), links))
                text.append("")
            if d["notes"]:
                text.append("**Tips and notes**"); text.append("")
                for n in d["notes"]: text.append("- " + autolink(re.sub(r'\s*\n\s*', ' ', n), links))
                text.append("")
        toc = []
        body = []
        for e in ents:
            if e.get("file"): continue
            level = 3 if (e["cls"] and e["kind"] in ("function", "field")) else 2
            md, anchor, heading = render_entry(e, level, links, seen)
            body.append(md); body.append("---" if level == 2 else "")
            if level == 2: toc.append(f"- [{heading}](#{anchor})")
        text += ["## Contents", ""] + toc + [""] + body
        pages[page] = "\n".join(text).rstrip() + "\n"
    os.makedirs(OUT, exist_ok=True)
    for page, txt in pages.items():
        open(os.path.join(OUT, page + ".md"), "w").write(txt)
    # index
    idx = ["# API index", "", "Every documented class, function and constant, alphabetically. (Generated by `tools/gen_docs.py`.)", "", "| Symbol | Kind | Page |", "|---|---|---|"]
    rows = []
    for h, _ in HEADERS:
        page = "API-" + h.replace(".h", "")
        for e in loaded[h]:
            if e.get("file"): continue
            if e["kind"] in ("struct", "class", "enum", "macro", "type") or (e["kind"] in ("function", "field") and not e["cls"]):
                rows.append((e["name"].lower(), e["name"], e["kind"], anchors.get(e["name"], page), h))
    for _, name, kind, link, h in sorted(rows):
        idx.append(f"| [`{name}`]({link}) | {kind} | {h} |")
    open(os.path.join(OUT, "API-Index.md"), "w").write("\n".join(idx) + "\n")
    # sidebar
    sb = ["**dd**", "", "- [Home](Home)", "- [Getting started](Getting-Started)", "- [Concepts](Concepts)", "- [Collision guide](Collision-Guide)",
          "- [Performance and memory](Performance-and-Memory)", "- [Troubleshooting](Troubleshooting)", "", "**API reference**", ""]
    for h, s in HEADERS: sb.append(f"- [{h}](API-{h.replace('.h', '')})")
    sb += ["- [Index](API-Index)"]
    open(os.path.join(OUT, "_Sidebar.md"), "w").write("\n".join(sb) + "\n")
    n_doc = sum(1 for h, _ in HEADERS for e in loaded[h] if not e.get("file"))
    print(f"wrote {len(pages)} API pages, {n_doc} documented symbols")
    if check:
        undocumented_report()

def undocumented_report():
    """Lists public declarations (types, functions, constructors) that have no preceding doc comment.
    Brace-depth aware: only looks at namespace-level and class-member-level lines, skips private/protected sections
    (except virtual hooks), overrides, deleted functions, defaulted members and plain fields."""
    miss = []
    kw = ("if", "for", "while", "switch", "return", "case", "else", "do", "static_assert", "using", "typedef", "friend")
    for h, _ in HEADERS:
        lines = open(os.path.join(SRC, h)).read().split("\n")
        depth = 0; in_comment = False; class_depth = None; access = "public"
        for n, raw in enumerate(lines):
            l = raw
            st = l.strip()
            if in_comment:
                if "*/" in st: in_comment = False
                continue
            if st.startswith("/*"):
                if "*/" not in st: in_comment = True
                continue
            code = re.sub(r'//.*$', '', l)
            code = re.sub(r'"(\\.|[^"\\])*"', '""', code)
            top = depth == 0
            member = class_depth is not None and depth == class_depth + 1
            if class_depth is not None and member and re.match(r'^\s*(public|protected|private)\s*:', st):
                access = st.split(":")[0].strip()
            decl = None
            if top or member:
                if re.match(r'^(struct|class)\s+\w+', st) and not st.endswith(";"):
                    decl = "type"
                elif re.match(r'^enum\b', st) or st.startswith("typedef "):
                    decl = "type"
                elif st.startswith("#ifndef DD_") or st.startswith("constexpr "):
                    decl = "const"
                elif "(" in st and not st.startswith(kw) and not st.startswith("#") and not st.startswith("}") and not st.startswith(":") \
                        and re.match(r'^(template|inline|static|explicit|virtual|constexpr|~|[\w:<>\*&,\s]+\s+[\*&]*[\w:~]+\s*\()', st) \
                        and not st.endswith(");") or (member and re.match(r'^(explicit\s+)?\w+\(.*\)\s*(:|\{)', st)):
                    decl = "func"
            if decl and not ("override" in st or "= delete" in st or "= default" in st or re.search(r'\w+::(\w+\s*[\(=]|operator)', st)
                            or st.startswith("inline void delayMs")):   # out-of-class definitions are documented at their declaration
                hidden = member and access != "public" and "virtual" not in st
                if st.startswith("template") and st.endswith(">"):
                    pass
                if not hidden and not (decl == "func" and st.startswith("template") is False and st.endswith(";") and "(" in st and top and "inline" not in st):
                    k = n - 1
                    while k >= 0 and lines[k].strip().startswith("template"): k -= 1
                    prev = lines[k].strip() if k >= 0 else ""
                    if not prev.endswith("*/") and not st.startswith("template"):
                        miss.append(f"{h}:{n+1}: {st[:90]}")
                    elif st.startswith("template"):
                        k2 = n - 1
                        if not (k2 >= 0 and lines[k2].strip().endswith("*/")):
                            miss.append(f"{h}:{n+1}: {st[:90]}")
            # update depth / class tracking
            opens = code.count("{"); closes = code.count("}")
            if class_depth is None and re.match(r'^(struct|class)\s+\w+', st) and "{" in code and not st.endswith(";"):
                class_depth = depth; access = "private" if st.startswith("class") else "public"
            depth += opens - closes
            if class_depth is not None and depth <= class_depth:
                class_depth = None
    if miss:
        print(f"\n{len(miss)} declarations without a doc comment:")
        for m in miss: print("  " + m)
    else:
        print("\nevery public declaration has a doc comment")

if __name__ == "__main__":
    main()
