from dayvault.proto import parse_list_output, parse_rec_name, local_tz_offset_minutes
from datetime import datetime, timedelta, timezone


def test_parse_list_output():
    text = "LIST mount=0\n  REC062.WAV 158476\n  REC-20260809-1925_0m05s.WAV 242880\nLIST done\n"
    assert parse_list_output(text) == [
        ("REC062.WAV", 158476),
        ("REC-20260809-1925_0m05s.WAV", 242880),
    ]


def test_parse_rec_name_timestamp_duration():
    r = parse_rec_name("REC-20260809-1925_1h23m45s.WAV")
    assert r["kind"] == "timestamp"
    assert r["timestamp"] == "2026-08-09 19:25"
    assert r["duration_secs"] == 1 * 3600 + 23 * 60 + 45
    assert r["collision"] == 0
    assert r["format"] == "wav"


def test_parse_opus_timestamp_duration():
    item = parse_rec_name("REC-20260811-0905_1h02m03s.OPUS")
    assert item["kind"] == "timestamp"
    assert item["timestamp"] == "2026-08-11 09:05"
    assert item["duration_secs"] == 3723
    assert item["collision"] == 0
    assert item["format"] == "opus"


def test_parse_mixed_case_opus_timestamp_collision_with_duration():
    item = parse_rec_name("REC-20260811-0905_2_0m05s.oPuS")
    assert item["kind"] == "timestamp"
    assert item["collision"] == 2
    assert item["duration_secs"] == 5
    assert item["format"] == "opus"


def test_parse_rec_name_timestamp_collision_no_duration():
    r = parse_rec_name("REC-20260809-1110_2.WAV")
    assert r["kind"] == "timestamp"
    assert r["collision"] == 2
    assert r["duration_secs"] is None


def test_parse_rec_name_plain_timestamp():
    r = parse_rec_name("REC-20260809-1110.WAV")
    assert r["kind"] == "timestamp"
    assert r["timestamp"] == "2026-08-09 11:10"
    assert r["collision"] == 0
    assert r["duration_secs"] is None


def test_parse_rec_name_timestamp_collision_with_duration():
    r = parse_rec_name("REC-20260809-1110_2_0m05s.WAV")
    assert r["kind"] == "timestamp"
    assert r["collision"] == 2
    assert r["duration_secs"] == 5


def test_parse_rec_name_timestamp_duration_only():
    r = parse_rec_name("REC-20260809-1110_5m32s.WAV")
    assert r["kind"] == "timestamp"
    assert r["collision"] == 0
    assert r["duration_secs"] == 332


def test_parse_rec_name_seq():
    r = parse_rec_name("REC062.WAV")
    assert r["kind"] == "seq"
    assert r["seq"] == 62
    assert r["format"] == "wav"


def test_parse_mixed_case_opus_seq():
    item = parse_rec_name("REC062.OpUs")
    assert item["kind"] == "seq"
    assert item["seq"] == 62
    assert item["duration_secs"] is None
    assert item["format"] == "opus"


def test_parse_rec_name_non_rec():
    assert parse_rec_name("TEST1.TXT") is None


def test_local_tz_offset_minutes_utc8():
    now = datetime(2026, 8, 10, 12, 0, tzinfo=timezone(timedelta(hours=8)))
    assert local_tz_offset_minutes(now) == 480
