#!/usr/bin/env python3
"""Answers agent auto-discovery broadcasts on UDP 47474.

Request : {"probe":"XEPHYGUARD_DISCOVER","v":1}
Response: {"service":"xephyguard-ccc","host":"192.168.10.2","api_port":8443,"ca_fingerprint":"<sha256 hex>","lab":"Lab 3"}

The reply is unauthenticated; agents therefore pin the CA fingerprint shown on the Add-a-PC page when one
is given (--ca-fingerprint), and every later connection is TLS-verified against that CA.
"""
import hashlib
import json
import logging
import os
import socket
import ssl

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
PORT = int(os.environ.get("XG_DISCOVERY_PORT", "47474"))
CA = os.environ.get("XG_CA_DIR", "/var/lib/xephyguard/ca") + "/ca.crt"


def fingerprint() -> str:
    try:
        der = ssl.PEM_cert_to_DER_cert(open(CA).read())
        return hashlib.sha256(der).hexdigest()
    except OSError:
        return ""


def main() -> None:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", PORT))
    logging.info("discovery responder on udp/%d", PORT)
    while True:
        data, addr = s.recvfrom(2048)
        try:
            req = json.loads(data)
        except ValueError:
            continue
        if req.get("probe") != "XEPHYGUARD_DISCOVER":
            continue
        reply = {"service": "xephyguard-ccc", "host": os.environ.get("XG_CCC_IP", ""), "api_port": int(os.environ.get("XG_AGENT_PORT", "8443")),
                 "ca_fingerprint": fingerprint(), "lab": os.environ.get("XG_LAB_NAME", "")}
        s.sendto(json.dumps(reply).encode(), addr)
        logging.info("answered %s", addr[0])


if __name__ == "__main__":
    main()
