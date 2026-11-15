# dd documentation

- **Wiki pages:** [`docs/wiki/`](wiki). Start at [Home](wiki/Home.md). Hand-written: Home, Getting-Started, Concepts, Collision-Guide, Performance-and-Memory, Troubleshooting.
  Generated: `API-*.md` (one per header), `API-Index.md`, `_Sidebar.md`.
- **Code comments:** every public class and function in `src/` has a Doxygen-style `/** ... */` block (purpose, parameters, return value, `@warning` pitfalls, `@note` tips) that IDEs show on hover.

## Regenerating the API pages

The API pages are generated **from those code comments**, so the code and the reference cannot drift apart. After changing a comment:

```
python3 tools/gen_docs.py            # rewrite docs/wiki/API-*.md, the index and the sidebar
python3 tools/gen_docs.py --check    # also list any public declaration that has no doc comment
```

Do not edit `API-*.md` by hand; edit the comment in `src/` and regenerate. The doc comment format: a brief first paragraph, free text, `@param name text`, `@tparam`, `@return`, `@warning` (pitfalls),
`@note` (tips), `@par Title` (a titled section), `@code ... @endcode`, and `@file` for a header's intro. Never put `*/` inside a comment (use `//` in examples).

## Publishing to the GitHub wiki

GitHub wikis are a separate git repository. After creating the first wiki page once in the browser (Wiki tab, "Create the first page", any content), run:

```
tools/publish_wiki.sh
```

It regenerates the pages, clones `dd.wiki.git`, copies `docs/wiki/*.md` into it and pushes. The same files also read fine directly in the repository.
