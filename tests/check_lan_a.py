"""Opt-in two-process LAN-A runner. Only the integrator executes this file.

No Python sockets/DNS, process enumeration, profile or game operations.
Popen owns each exact Windows process handle; timeout kills only those children.
Logs contain bounded operation metadata, never payload bytes.
"""
import argparse
from pathlib import Path
import queue
import re
import subprocess
import threading
import time


CASES = {
    "baseline": ("none", "none", None, None, None),
    "client-bind": ("none", "client-bind", "client", 86, "FAIL HOST client-unbound"),
    "reply-endpoint": ("reply-endpoint", "none", "server", 86, "FAIL HOST reply-endpoint16"),
    "omit-reply": ("omit-reply", "none", "client", 1, "FAIL GUEST reply-ready"),
    "dirty-address": ("dirty-address", "none", "server", 1, "FAIL GUEST address-clean"),
    "short-address": ("short-address", "none", "server", 1, "FAIL GUEST address-length16"),
    "lost-source-port": ("lost-source-port", "none", "server", 1, "FAIL GUEST address-clean"),
    "errno98": ("errno98", "none", "server", 1, "FAIL GUEST conflict-errno98"),
    "omit-close": ("omit-close", "none", "server", 1, "FAIL GUEST double-close"),
}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


class Child:
    """Drain both pipes concurrently; keep at most 64 KiB per stream."""
    def __init__(self, command, role):
        self.role = role
        self.lines = queue.Queue()
        self.output = {"stdout": bytearray(), "stderr": bytearray()}
        self.overflow = False
        self.forced = False
        self.process = subprocess.Popen(
            command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        self.threads = []
        for stream in ("stdout", "stderr"):
            thread = threading.Thread(target=self.drain, args=(stream,), daemon=True)
            thread.start()
            self.threads.append(thread)

    def drain(self, stream):
        pipe = getattr(self.process, stream)
        pending = bytearray()
        while True:
            chunk = pipe.read(1)  # bounded lines; no unbounded readline on a broken child
            if not chunk:
                break
            dest = self.output[stream]
            if len(dest) < 65536:
                dest.extend(chunk)
            else:
                self.overflow = True
            if len(pending) < 4096:
                pending.extend(chunk)
            else:
                self.overflow = True
            if chunk == b"\n":
                if stream == "stdout" and self.lines.qsize() < 512:
                    self.lines.put(pending.decode("utf-8", errors="replace").strip())
                pending.clear()
        pipe.close()

    def wait(self, deadline):
        self.process.wait(timeout=max(0.01, deadline - time.monotonic()))
        for thread in self.threads:
            thread.join(timeout=max(0.01, deadline - time.monotonic()))
        require(not any(t.is_alive() for t in self.threads), "output drain timed out")
        require(not self.overflow, "child metadata exceeded bound")

    def terminate_owned(self):
        if self.process.poll() is None:
            self.forced = True
            self.process.kill()  # exact retained Popen handle, never a PID/name search

    def finish_cleanup(self):
        errors = []
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            errors.append(self.role + " did not exit after owned-handle termination")
        for thread in self.threads:
            thread.join(timeout=0.5)
        if any(thread.is_alive() for thread in self.threads):
            errors.append(self.role + " output drain incomplete")
        return errors

    def text(self, stream):
        return bytes(self.output[stream]).decode("utf-8", errors="replace")

    def save(self, directory):
        for stream in ("stdout", "stderr"):
            (directory / (self.role + "." + stream + ".log")).write_bytes(bytes(self.output[stream]))
        (directory / (self.role + ".exit.txt")).write_text(
            "returncode={} forced={} overflow={}\n".format(
                self.process.returncode, self.forced, self.overflow), encoding="utf-8")


def wait_ready(child, deadline):
    while time.monotonic() < deadline:
        require(not child.overflow, "child output overflow before readiness")
        try:
            line = child.lines.get(timeout=0.05)
        except queue.Empty:
            if child.process.poll() is not None:
                return False
            continue
        if line == "READY 7891 loopback=127.0.0.1":
            return True
    raise RuntimeError("server readiness deadline exceeded")


def one_port(child, label):
    values = re.findall(r"^EVENT " + label + r" (\d+) 0\r?$", child.text("stdout"), re.M)
    require(len(values) == 1, "missing or repeated " + label)
    return int(values[0])


def run(args):
    # A fresh, caller-owned log directory prevents stale-output acceptance.
    args.logs.mkdir(parents=True, exist_ok=False)
    server_fault, client_fault, expected_role, expected_code, expected_marker = CASES[args.case]
    children = []
    started = time.monotonic()
    deadline = started + 10
    outcome = "FAIL"
    try:
        server = Child([str(args.exe), str(args.fixture), "server", server_fault], "server")
        children.append(server)
        ready = wait_ready(server, min(deadline, started + 4))
        if not ready:
            server.wait(deadline)
            if server.process.returncode == 77 and "EVENT BLOCKED_PORT 7891 " in server.text("stdout"):
                outcome = "BLOCKED: fixed UDP7891 unavailable; no substitute port or owner interference"
                print(outcome)
                return 77
            require(args.case in ("errno98", "omit-close"), "server failed before READY")
            require(server.process.returncode == expected_code and expected_marker in server.text("stderr"),
                    "setup mutation did not reach its required assertion")
            require("INJECT " + args.case in server.text("stdout"), "setup mutation not reached")
        else:
            require(args.case not in ("errno98", "omit-close"), "setup mutation unexpectedly reached READY")
            client = Child([str(args.exe), str(args.fixture), "client", client_fault], "client")
            children.append(client)
            client.wait(deadline)
            server.wait(deadline)
            require(server.process.pid != client.process.pid, "distinct process ownership")
            if args.case == "baseline":
                for child in children:
                    out, err = child.text("stdout"), child.text("stderr")
                    require(child.process.returncode == 0 and not err and "INJECT" not in out,
                            "baseline child failed: " + child.role)
                    require("PASS " + child.role + " imports=production fault=0 binds=0" in out,
                            "missing production import completion")
                    require(len(re.findall(r"^EVENT ROUND ", out, re.M)) == 3 and
                            "EVENT CLOSED 0 0" in out and "EVENT IMMEDIATE " in out,
                            "incomplete repeated exchange/close")
                port = one_port(client, "CLIENT_PORT")
                require(port not in (0, 7891) and one_port(server, "PEER_PORT") == port,
                        "independent endpoints or observed reply source lost")
                require("PRE_SEND unbound=1 binds=0" in client.text("stdout"), "implicit bind not proved")
                require("EVENT CONFLICT 98 0" in server.text("stdout"), "owned conflict not proved")
            else:
                target = server if expected_role == "server" else client
                require(target.process.returncode == expected_code and expected_marker in target.text("stderr"),
                        "mutation did not fail its required assertion")
                # A peer may reach its bounded read timeout after the other side
                # fails. Crashes, import traps and arbitrary errors never count.
                peer = client if target is server else server
                if args.case == "omit-reply":
                    require(peer.process.returncode == 0 and "INJECT omit-reply" in peer.text("stdout") and
                            "PASS server imports=production fault=3" in peer.text("stdout"),
                            "missing-reply omission was not exercised")
                else:
                    peer_marker = "FAIL GUEST reply-ready" if peer is client else "FAIL GUEST receive-ready"
                    require(peer.process.returncode == 1 and peer_marker in peer.text("stderr"),
                            "peer did not finish through its bounded timeout")
                if args.case in ("dirty-address", "short-address", "lost-source-port"):
                    require("INJECT " + args.case in server.text("stdout"), "output mutation not reached")
        require(not any(c.forced for c in children), "forced cleanup cannot pass")
        outcome = "PASS " + args.case + " (synthetic imported socket shape only)"
        print(outcome)
        return 0
    finally:
        cleanup_errors = []
        # Terminate all still-live owned children before any cleanup wait; an
        # error on one handle must never leave the other child running.
        for child in children:
            try:
                child.terminate_owned()
            except OSError as error:
                cleanup_errors.append(child.role + " termination: " + str(error))
        for child in children:
            cleanup_errors.extend(child.finish_cleanup())
            child.save(args.logs)
        if cleanup_errors or any(c.forced for c in children):
            outcome = "FAIL: owned child cleanup required or incomplete"
        (args.logs / "result.txt").write_text(
            outcome + "\nelapsed_seconds={:.3f}\n".format(time.monotonic() - started), encoding="utf-8")
        require(not cleanup_errors, "; ".join(cleanup_errors))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--case", required=True, choices=CASES)
    parser.add_argument("--logs", required=True, type=Path)
    args = parser.parse_args()
    try:
        args.exe = args.exe.resolve(strict=True)
        args.fixture = args.fixture.resolve(strict=True)
        return run(args)
    except (RuntimeError, subprocess.TimeoutExpired, OSError) as error:
        print("FAIL LAN-A:", error)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
