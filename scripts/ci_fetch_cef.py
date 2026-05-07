#!/usr/bin/env python3
"""Download a CEF minimal binary tarball from Spotify CDN and extract it.

Usage:
  python scripts/ci_fetch_cef.py <url> <parent_dir>

Prints the absolute path to the extracted cef_binary_* root (single line on stdout).
See https://cef-builds.spotifycdn.com/index.html
"""
from __future__ import annotations

import shutil
import sys
import tarfile
import urllib.request
from pathlib import Path


def main() -> int:
	if len(sys.argv) != 3:
		print("usage: ci_fetch_cef.py <tar.bz2_url> <extract_parent_dir>", file=sys.stderr)
		return 2

	url = sys.argv[1]
	parent = Path(sys.argv[2]).resolve()
	parent.mkdir(parents=True, exist_ok=True)

	archive = parent / "cef_dist.tar.bz2"
	print(f"Downloading CEF from {url}", file=sys.stderr)
	req = urllib.request.Request(
		url,
		headers={"User-Agent": "cs16-client-ci/1.0"},
	)
	with urllib.request.urlopen(req, timeout=600) as resp:
		with archive.open("wb") as out:
			shutil.copyfileobj(resp, out)

	print(f"Extracting to {parent}", file=sys.stderr)
	with tarfile.open(archive, "r:bz2") as tf:
		# filter= exists in Python 3.12+
		kw = {}
		if sys.version_info >= (3, 12):
			kw["filter"] = "data"
		tf.extractall(parent, **kw)

	archive.unlink(missing_ok=True)

	for p in sorted(parent.iterdir()):
		if p.is_dir() and p.name.startswith("cef_binary_"):
			print(p.resolve().as_posix() if sys.platform != "win32" else str(p.resolve()))
			return 0

	print("No cef_binary_* directory found after extract", file=sys.stderr)
	return 1


if __name__ == "__main__":
	sys.exit(main())
