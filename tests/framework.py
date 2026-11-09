#!/usr/bin/env python3
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCES = [p for p in (ROOT / "framework/pa4").glob("*.cc")
           if p.name not in ("main.cc", "noc_main.cc")]

with tempfile.TemporaryDirectory() as directory:
    executable = pathlib.Path(directory) / "framework-test"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-O1", "-g",
                    "-I" + str(ROOT / "include"), *map(str, SOURCES),
                    str(ROOT / "tests/framework.cc"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
