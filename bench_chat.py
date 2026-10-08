#!/usr/bin/env python3
"""
mini-chat benchmark: N clients in a room, one blaster sends M messages,
measures fan-out throughput and completeness.
"""
import argparse, socket, threading, time

HOST = "127.0.0.1"


def client(port, room, idx, expected, start_evt, counts, errors):
    try:
        s = socket.create_connection((HOST, port), timeout=30)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        s.sendall(f"/nick user{idx}\n/join {room}\n".encode())
        start_evt.wait()
        buf = b""
        got = 0
        while got < expected:
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                if b"BENCH " in line:
                    got += 1
        counts[idx] = got
        s.close()
    except Exception as e:
        errors.append(f"user{idx}: {e}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", type=int)
    ap.add_argument("--clients", type=int, default=100)
    ap.add_argument("--messages", type=int, default=2000)
    ap.add_argument("--room", default="bench")
    ap.add_argument("--timeout", type=float, default=30.0)
    args = ap.parse_args()

    N, M = args.clients, args.messages
    start_evt = threading.Event()
    counts = [0] * N
    errors = []
    threads = []

    print(f"Connecting {N} clients to #{args.room}...")
    for i in range(N):
        t = threading.Thread(
            target=client,
            args=(args.port, args.room, i, M, start_evt, counts, errors),
            daemon=True,
        )
        t.start()
        threads.append(t)

    time.sleep(2.0)

    blaster = socket.create_connection((HOST, args.port), timeout=30)
    blaster.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    blaster.sendall(f"/nick blaster\n/join {args.room}\n".encode())
    time.sleep(0.5)

    print(f"Blasting {M} messages...")
    start_evt.set()
    time.sleep(0.2)

    t0 = time.perf_counter()
    payload = b"".join(f"BENCH {i}\n".encode() for i in range(M))
    blaster.sendall(payload)
    send_done = time.perf_counter()

    deadline = time.time() + args.timeout
    while time.time() < deadline:
        if all(c >= M for c in counts):
            break
        time.sleep(0.02)

    t1 = time.perf_counter()

    total = sum(counts)
    expected = N * M
    complete = sum(1 for c in counts if c >= M)

    print()
    print(f"Clients:            {N}")
    print(f"Messages sent:      {M}")
    print(f"Clients complete:   {complete}/{N}")
    print(f"Messages delivered: {total}/{expected}")
    print(f"Send time:          {(send_done - t0) * 1000:.1f} ms")
    print(f"Total time:         {(t1 - t0) * 1000:.1f} ms")
    if t1 > t0:
        print(f"Fan-out rate:       {total / (t1 - t0):.0f} msg/s")
    if errors:
        print(f"Errors:             {len(errors)} ({errors[0]})")


if __name__ == "__main__":
    main()
