"""Read-only FTP capture of a decompressed Xbox cache's header and tag image."""
import argparse
from ftplib import FTP
from pathlib import Path
import struct


def read_range(host, port, path, offset, size):
    ftp = FTP()
    ftp.connect(host, port, timeout=20)
    ftp.login()
    ftp.voidcmd('TYPE I')
    result = bytearray()
    try:
        with ftp.transfercmd('RETR '+path, rest=offset if offset else None) as sock:
            while len(result) < size:
                chunk = sock.recv(min(65536, size-len(result)))
                if not chunk:
                    raise RuntimeError('short FTP read')
                result.extend(chunk)
    finally:
        # This is a bounded read, not a complete RETR; discard the connection.
        ftp.close()
    return bytes(result)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--host', required=True)
    p.add_argument('--port', type=int, default=1337)
    p.add_argument('--remote', required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    header = read_range(a.host, a.port, a.remote, 0, 2048)
    signature, version, length, _, offset, size = struct.unpack_from('<6I', header)
    assert signature == 0x68656164 and version == 5
    assert 0 < size <= 22*1024*1024 and offset+size <= length
    tags = read_range(a.host, a.port, a.remote, offset, size)
    a.out.mkdir(parents=True, exist_ok=True)
    for name, contents in [('cache-header.bin', header), ('tags.bin', tags)]:
        with (a.out/name).open('xb') as f:
            f.write(contents)
    print(f'Captured {size} tag bytes from offset {offset:#x}; remote unchanged')


if __name__ == '__main__':
    main()
