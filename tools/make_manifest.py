#!/usr/bin/env python3
import argparse, hashlib, json, pathlib, sys

try:
    from cryptography.hazmat.primitives.asymmetric import ec, utils
    from cryptography.hazmat.primitives import hashes, serialization
except ImportError:
    ec = utils = hashes = serialization = None


def canonical(m):
    # MUST match build_signing_payload() in the firmware byte-for-byte.
    return (
        f"device_model={m['device_model']}\n"
        f"version={m['version']}\n"
        f"size={m['size']}\n"
        f"sha256={m['sha256']}\n"
        f"firmware_url={m['firmware_url']}\n"
    ).encode()


def print_pubkey_c_array(key):
    pub = key.public_key().public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )
    assert len(pub) == 65 and pub[0] == 0x04
    print('static const uint8_t g_manifest_pubkey[65] = {')
    for i in range(0, 65, 8):
        chunk = pub[i:i + 8]
        print('    ' + ' '.join(f'0x{b:02x},' for b in chunk))
    print('};')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--firmware', required=True)
    ap.add_argument('--version', type=int, required=True)
    ap.add_argument('--base-url', required=True, help='Example: http://192.168.1.10:8000')
    ap.add_argument('--model', default='ce-40876')
    ap.add_argument('--key', default=None,
                    help='ECDSA P-256 private key (PEM). If omitted, manifest is NOT signed.')
    ap.add_argument('--out', default='manifest.json')
    ap.add_argument('--bad-signature', action='store_true',
                    help='Corrupt the signature (to test rejection).')
    ap.add_argument('--print-pubkey', action='store_true',
                    help='Also print the C public-key array for g_manifest_pubkey[].')
    args = ap.parse_args()

    fw_path = pathlib.Path(args.firmware)
    data = fw_path.read_bytes()
    m = {
        'device_model': args.model,
        'version': args.version,
        'size': len(data),
        'sha256': hashlib.sha256(data).hexdigest(),
        'firmware_url': args.base_url.rstrip('/') + '/' + fw_path.name,
    }

    if args.key:
        if serialization is None:
            print('Install cryptography: python3 -m pip install cryptography', file=sys.stderr)
            sys.exit(2)
        key_path = pathlib.Path(args.key)
        key = serialization.load_pem_private_key(key_path.read_bytes(), password=None)

        # Sign the canonical payload (the WHOLE manifest content, not just the binary).
        der = key.sign(canonical(m), ec.ECDSA(hashes.SHA256()))
        r, s = utils.decode_dss_signature(der)
        sig = r.to_bytes(32, 'big') + s.to_bytes(32, 'big')  # raw r||s, 64 bytes
        if args.bad_signature:
            sig = bytes([sig[0] ^ 1]) + sig[1:]
        m['signature'] = sig.hex()

        print_pubkey_c_array(key)
        print()
    else:
        print('WARNING: no --key given, manifest will be unsigned and rejected by the device.',
              file=sys.stderr)

    pathlib.Path(args.out).write_text(json.dumps(m, indent=2), encoding='utf-8')
    print(f'wrote {args.out}')
    print(json.dumps(m, indent=2))


if __name__ == '__main__':
    main()