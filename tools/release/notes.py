"""Assemble release notes with documentation links pinned to the released source."""
import argparse
from pathlib import Path
import re
from urllib.parse import quote, unquote, urlsplit

ROOT = Path(__file__).resolve().parents[2]


def source_links(contents, document, repository, revision):
    def replace(match):
        target = urlsplit(match[2].strip().strip("<>"))
        if target.scheme or target.netloc or not target.path:
            return match[0]
        source = (document.parent / unquote(target.path)).resolve()
        relative = source.relative_to(ROOT).as_posix()
        if not source.is_file():
            raise ValueError(f"Missing release documentation: {relative}")
        url = f"https://github.com/{repository}/blob/{revision}/{quote(relative, safe='/')}"
        if target.fragment:
            url += "#" + target.fragment
        return f"{match[1]}({url})"
    return re.sub(r"(\[[^\]]*\])\(([^)]+)\)", replace, contents)


def render(version, repository, revision):
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("Expected a full source commit")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Expected owner/repository")
    documents = [ROOT / f"docs/releases/{version}.md",
                 ROOT / "tools/release/INSTALL-linux.md",
                 ROOT / "tools/release/INSTALL-windows.md"]
    sections = [source_links(path.read_text(encoding="utf-8"), path, repository, revision).rstrip()
                for path in documents]
    return "\n\n".join(sections) + f"\n\nSource commit: `{revision}`\n"


if __name__ == "__main__":
    import package

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(render(package.identity()["version"], args.repository, args.revision), encoding="utf-8")
