#!/usr/bin/env python3
"""Run the broad core suite in disposable Linux network namespaces.

Invoke as root with --build-dir and --user. The ordinary suite runs as the
specified unprivileged user; only the legacy network-mutating IPv4 case runs
as root. No test assertions are changed. XML results are written by GoogleTest
under /tmp and their paths are printed for subsequent inspection.
"""
import argparse
import glob
import os
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

NETWORK_CASE = "test_ipv4addressiterator.simple_ipv4addressiterator"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--user", default="aditi")
    parser.add_argument("--inside", choices=("ordinary", "network"))
    parser.add_argument("--report", action="store_true")
    args = parser.parse_args()
    if args.report:
        for path in sorted(glob.glob("/tmp/thunder-broad-*.xml")):
            root = ET.parse(path).getroot()
            print(f"XML={path}; totals={root.attrib}", flush=True)
            counts = {"passed": 0, "failed": 0, "skipped": 0, "disabled": 0}
            for suite in root.findall("testsuite"):
                for case in suite.findall("testcase"):
                    failure = case.find("failure")
                    if case.get("status") == "notrun":
                        status = "disabled"
                    elif failure is not None:
                        status = "failed"
                    elif case.find("skipped") is not None:
                        status = "skipped"
                    else:
                        status = "passed"
                    counts[status] += 1
                    if status != "passed":
                        print(f"{suite.get('name')}.{case.get('name')}: {status}", flush=True)
                    if failure is not None:
                        print(failure.get("message", ""), flush=True)
            print(f"CaseCounts={counts}", flush=True)
        for path in glob.glob(os.path.join(args.build_dir, "Testing/Temporary/LastTest.log*")):
            with open(path, "rb") as stream:
                text = stream.read().decode("utf-8", errors="replace")
            print(f"CTestLog={path}", flush=True)
            lines = text.splitlines()
            for index, line in enumerate(lines):
                if "Failure" in line or "[  FAILED" in line or "Timeout" in line:
                    print("\n".join(lines[max(0, index - 2):index + 12]), flush=True)
            print("\n".join(lines[-25:]), flush=True)
        return 0
    if os.geteuid() != 0:
        parser.error("root is required to create disposable network namespaces")
    if args.inside is None:
        result = 0
        for mode in ("ordinary", "network"):
            command = [
                "unshare", "--net", "--", sys.executable, "-u",
                os.path.abspath(__file__), "--build-dir", args.build_dir,
                "--user", args.user, "--inside", mode,
            ]
            code = subprocess.call(command)
            print(f"PartitionExit[{mode}]={code}", flush=True)
            if code:
                result = code
        return result
    for command in (
        ["ip", "link", "set", "lo", "up"],
        ["ip", "link", "add", "eth0", "type", "dummy"],
        ["ip", "link", "set", "eth0", "up"],
    ):
        subprocess.run(command, check=True)
    stamp = f"{time.time_ns()}-{args.inside}"
    output = f"/tmp/thunder-broad-{stamp}.xml"
    test_filter = f"-{NETWORK_CASE}" if args.inside == "ordinary" else NETWORK_CASE
    command = [
        "env", "CI=true", f"GTEST_FILTER={test_filter}",
        f"GTEST_OUTPUT=xml:{output}", "ctest", "--test-dir", args.build_dir,
        "-V", "-R", "^Thunder_test_core$", "--timeout", "600",
    ]
    if args.inside == "ordinary":
        command = ["runuser", "-u", args.user, "--"] + command
    print(f"Partition={args.inside}; XML={output}; Filter={test_filter}", flush=True)
    start = time.monotonic()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    for line in iter(process.stdout.readline, b""):
        print(line.decode("utf-8", errors="replace"), end="", flush=True)
    code = process.wait()
    print(f"PartitionSeconds[{args.inside}]={time.monotonic() - start:.2f}", flush=True)
    print(f"XMLResult={output}", flush=True)
    return code


if __name__ == "__main__":
    sys.exit(main())
