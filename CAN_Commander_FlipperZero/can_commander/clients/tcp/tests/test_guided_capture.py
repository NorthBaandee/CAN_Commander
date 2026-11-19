import json
import os
import pty
import select
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest


GREETING = (
    b"WiFi CLI client connected\r\n"
    b"WiFi CLI ready. Commands: help\r\n"
)


class GuidedCaptureTest(unittest.TestCase):
    def setUp(self):
        self.binary = sys.argv[1]
        self.temp_dir = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.temp_dir.cleanup()

    def start_client(self, port, path):
        master, slave = pty.openpty()
        process = subprocess.Popen(
            [self.binary, "capture", "127.0.0.1", path, str(port)],
            stdin=slave,
            stdout=slave,
            stderr=slave,
            close_fds=True,
        )
        os.close(slave)
        return process, master

    def wait_for_text(self, master, output, text, occurrence=1, timeout=4):
        target = text.encode()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if output.count(target) >= occurrence:
                return
            ready, _, _ = select.select(
                [master], [], [], max(0, deadline - time.monotonic())
            )
            if not ready:
                break
            try:
                output.extend(os.read(master, 4096))
            except OSError:
                break
        self.fail(f"missing output {text!r}: {output.decode(errors='replace')}")

    @staticmethod
    def start_server(handler):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        port = listener.getsockname()[1]
        errors = []

        def serve():
            try:
                conn, _ = listener.accept()
                conn.settimeout(5)
                handler(conn)
                conn.close()
            except Exception as exc:
                errors.append(str(exc))
            finally:
                listener.close()

        thread = threading.Thread(target=serve)
        thread.start()
        return port, thread, errors

    def test_records_action_cues_and_passive_frames(self):
        commands = []

        def handler(conn):
            conn.sendall(GREETING)
            reader = conn.makefile("rb")
            commands.append(reader.readline().decode().strip())
            conn.sendall(b"no active tool\r\n")
            commands.append(reader.readline().decode().strip())
            conn.sendall(
                b"tool started: read_all\r\n"
                b"bus=can0 id=0x1D5 std data dlc=2 data=10 20\r\n"
            )
            time.sleep(0.2)
            conn.sendall(b"bus=can1 id=0x18DAF110 ext data dlc=3 data=01 A0 FF\r\n")
            commands.append(reader.readline().decode().strip())
            conn.sendall(b"tool stopped\r\n")

        port, server, errors = self.start_server(handler)
        path = os.path.join(self.temp_dir.name, "capture.jsonl")
        process, master = self.start_client(port, path)
        output = bytearray()
        try:
            self.wait_for_text(master, output, "Action cue:", 1)
            os.write(master, b"baseline idle\n")
            self.wait_for_text(master, output, "Action cue:", 2)
            os.write(master, b"press accelerator slowly\n")
            self.wait_for_text(master, output, "Action cue:", 3)
            time.sleep(0.35)
            os.write(master, b"/done\n")
            process.wait(timeout=5)
            server.join(timeout=5)

            self.assertEqual(process.returncode, 0, output.decode(errors="replace"))
            self.assertFalse(errors, errors)
            self.assertEqual(
                commands,
                [
                    "tool status",
                    "tool start read_all bus=can0 ascii=0",
                    "tool stop",
                ],
            )
            with open(path, encoding="utf-8") as capture:
                records = [json.loads(line) for line in capture]
            self.assertEqual(records[0]["type"], "session_start")
            self.assertEqual(records[-1]["type"], "session_end")
            frames = [record for record in records if record["type"] == "can_frame"]
            actions = [record for record in records if record["type"] == "action"]
            self.assertEqual(len(frames), 2)
            self.assertEqual(frames[0]["data"], ["10", "20"])
            self.assertEqual(frames[1]["bus"], "can1")
            self.assertTrue(frames[1]["extended"])
            self.assertEqual(
                [record["label"] for record in actions],
                ["baseline idle", "press accelerator slowly"],
            )
            timestamps = [record["timestamp_ms"] for record in records]
            self.assertEqual(timestamps, sorted(timestamps))
            self.assertEqual(records[-1]["frames"], 2)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=2)
            if server.is_alive():
                server.join(timeout=2)
            os.close(master)

    def test_refuses_to_replace_an_active_tool(self):
        commands = []

        def handler(conn):
            conn.sendall(GREETING)
            reader = conn.makefile("rb")
            commands.append(reader.readline().decode().strip())
            conn.sendall(b"write bus=can0 id=0x1D5\r\n")
            reader.readline()

        port, server, errors = self.start_server(handler)
        path = os.path.join(self.temp_dir.name, "capture.jsonl")
        process, master = self.start_client(port, path)
        output = bytearray()
        try:
            process.wait(timeout=5)
            server.join(timeout=5)
            self.wait_for_text(master, output, "Refusing to replace")
            self.assertNotEqual(process.returncode, 0)
            self.assertEqual(commands, ["tool status"])
            self.assertFalse(errors, errors)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=2)
            if server.is_alive():
                server.join(timeout=2)
            os.close(master)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
