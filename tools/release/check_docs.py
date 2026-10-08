"""Check repository-relative links in maintained Markdown documentation."""
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[2]


def broken_links(root):
    documents = [*root.glob("README*.md"), *root.glob("docs/**/*.md"), *root.glob("tools/**/*.md")]
    errors = []
    for document in sorted(documents):
        contents = document.read_text(encoding="utf-8")
        for match in re.finditer(r"\[[^\]]*\]\(([^)]+)\)", contents):
            target = match[1].strip().strip("<>")
            link = urlsplit(target)
            if link.scheme or link.netloc or not link.path:
                continue
            if not (document.parent / unquote(link.path)).exists():
                line = contents[:match.start()].count("\n") + 1
                errors.append(f"{document.relative_to(root)}:{line}: {target}")
    return errors


if __name__ == "__main__":
    errors = broken_links(ROOT)
    if errors:
        raise SystemExit("Broken documentation links:\n" + "\n".join(errors))
    print("Documentation links passed")
