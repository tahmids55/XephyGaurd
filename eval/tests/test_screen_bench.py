import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "harness"))
import screen_bench as sb  # noqa: E402


def test_cmd_pins_only_when_cpu_given():
    c = sb.screenbench_cmd("/x/sb", "127.0.0.1:1", "/x/b", "typing", 5, "x11", "0")
    assert c[-2:] == ["-cpu", "0"] and "-capture" in c and c[c.index("-workload") + 1] == "typing"
    assert "-cpu" not in sb.screenbench_cmd("/x/sb", "g", "b", "idle", 5, "synthetic", "")


def test_table_has_a_row_per_workload_and_survives_missing_streamer():
    rows = [{"workload": "typing", "viewer_frames": 24, "drops": 0, "latency_ms_p50": 6.2, "latency_ms_p95": 8.7,
             "viewer_bytes_per_s": 37000, "streamer": {"fps": 4.8, "cpu_s_per_s": 0.06, "wakeups_per_s": 96}},
            {"workload": "idle"}]
    lines = sb.table(rows).splitlines()
    assert len(lines) == 3 and "typing" in lines[1] and "6.2" in lines[1] and "idle" in lines[2]


def test_missing_bench_binary_is_reported(tmp_path, capsys):
    assert sb.main(["--bench", str(tmp_path / "nope"), "--out", str(tmp_path)]) == 2
    assert "missing" in capsys.readouterr().err


def test_free_port_is_usable():
    assert 0 < sb.free_port() < 65536
