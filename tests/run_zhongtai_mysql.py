"""Run ZhongTai storage tests against an ephemeral, isolated MySQL instance (Linux)."""
import argparse
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="zhongtai-mysql-") as directory:
        root = Path(directory)
        data = root / "data"
        data.mkdir()
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        subprocess.run(["mysqld", "--no-defaults", "--initialize-insecure", "--datadir=" + str(data), "--log-error=" + str(root / "init.log")], check=True)
        log = (root / "server.log").open("wb")
        command = ["mysqld", "--no-defaults", "--user=" + __import__("pwd").getpwuid(os.getuid()).pw_name, "--datadir=" + str(data), "--socket=" + str(root / "mysql.sock"), "--port=" + str(port), "--bind-address=127.0.0.1", "--mysqlx=OFF", "--pid-file=" + str(root / "pid"), "--log-error=" + str(root / "error.log")]
        process = subprocess.Popen(command, stdout=log, stderr=log)
        try:
            for _ in range(100):
                if process.poll() is not None:
                    raise RuntimeError((root / "error.log").read_text())
                ready = subprocess.run(["mysqladmin", "--no-defaults", "--socket=" + str(root / "mysql.sock"), "-uroot", "ping"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if ready.returncode == 0:
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("isolated MySQL startup timed out")
            env = os.environ.copy()
            env.update(ZHONGTAI_ISOLATED_MYSQL="1", ZHONGTAI_MYSQL_PORT=str(port))
            subprocess.run([str(args.binary.resolve())], env=env, check=True)
        finally:
            process.terminate()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            log.close()
if __name__ == "__main__":
    main()
