#!/usr/bin/env python3
"""Write a CTest JUnit summary, keeping skipped graphics tests visible in CI."""

import argparse
from pathlib import Path
import xml.etree.ElementTree as ET


def summarize(results: Path) -> str:
    if not results.is_file():
        raise ValueError("CTest did not produce a test report; inspect the test log.")
    cases = list(ET.parse(results).iter("testcase"))
    if not cases:
        raise ValueError("CTest reported no tests.")
    skipped = [case for case in cases if case.find("skipped") is not None]
    failed = [case for case in cases
              if case.find("failure") is not None or case.find("error") is not None]
    passed = len(cases) - len(skipped) - len(failed)
    lines = ["## macOS Build & Tests", "",
             f"**{passed} passed, {len(failed)} failed, {len(skipped)} skipped** "
             f"({len(cases)} CTest tests).", ""]
    for label, tests in [("Failed", failed), ("Skipped", skipped)]:
        if tests:
            lines += [f"### {label}", ""]
            lines += [f"- `{case.get('name', 'unnamed')}`" for case in tests]
            lines.append("")
    if skipped:
        lines += ["Graphics tests can skip when the runner lacks a suitable GPU or desktop "
                  "session. Skips are not passes; native rendering still needs validation "
                  "on a Mac before release."]
        print(f"::warning::{len(skipped)} CTest tests were skipped. See the job summary.")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        args.output.write_text(summarize(args.results), encoding="utf-8")
    except (OSError, ValueError, ET.ParseError) as error:
        parser.exit(1, f"Cannot summarize CTest results: {error}\n")


if __name__ == "__main__":
    main()
