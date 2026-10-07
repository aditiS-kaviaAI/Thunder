"""Isolate OpenSSL default trust tests without modifying machine trust or .env."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


# PUBLIC_INTERFACE
def main():
    """Generate a temporary CA and run the TLS regression; return its exit code."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--openssl", required=True)
    parser.add_argument("--test", required=True)
    parser.add_argument("--source", choices=("bundle", "directory"), required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="thunder-p1-trust-") as temporary:
        root = Path(temporary)
        certificate = root / "root.pem"
        key = root / "key.pem"
        directory = root / "certs"
        directory.mkdir()
        subprocess.run(
            [args.openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes",
             "-days", "1", "-subj", "/CN=P1 default trust fixture",
             "-addext", "subjectAltName=DNS:localhost",
             "-addext", "basicConstraints=critical,CA:TRUE",
             "-keyout", str(key), "-out", str(certificate)],
            check=True, timeout=15, stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        # Both defaults are overridden so host-installed roots cannot hide a defect.
        bundle = certificate
        if args.source == "directory":
            digest = subprocess.run(
                [args.openssl, "x509", "-in", str(certificate), "-hash", "-noout"],
                check=True, timeout=5, capture_output=True, text=True,
            ).stdout.strip()
            (directory / (digest + ".0")).write_bytes(certificate.read_bytes())
            bundle = root / "empty.pem"
            bundle.write_text("", encoding="ascii")
        environment = os.environ.copy()
        environment.update(
            SSL_CERT_FILE=str(bundle), SSL_CERT_DIR=str(directory),
            P1_TLS_CERT=str(certificate), P1_TLS_KEY=str(key),
        )
        return subprocess.run(
            [args.test, "--gtest_filter=TLSDefaultTrustP1.*"],
            env=environment, timeout=15, check=False,
        ).returncode


if __name__ == "__main__":
    raise SystemExit(main())
