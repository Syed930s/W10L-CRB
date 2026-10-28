python3 - << 'PY'
paths = ["/etc/pki/tls/certs/ca-bundle.crt", "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem", "/etc/ssl/certs/ca-certificates.crt"]
data = None
for p in paths:
    try:
        data = open(p, "rb").read()
        break
    except OSError:
        pass
if data is None:
    raise SystemExit("no system CA bundle found")
out = ["#pragma once", "static const unsigned char CA_BUNDLE_DATA[] = {"]
line = []
for b in data:
    line.append("0x%02x," % b)
    if len(line) == 20:
        out.append("".join(line))
        line = []
if line:
    out.append("".join(line))
out.append("};")
out.append("static const unsigned long CA_BUNDLE_LEN = %d;" % len(data))
open("src/ca_bundle.h", "w").write("\n".join(out) + "\n")
PY
