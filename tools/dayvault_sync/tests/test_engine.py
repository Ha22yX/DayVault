from dayvault.engine import plan_downloads


def test_plan_downloads_new_and_changed():
    remote = [("A.WAV", 100), ("B.WAV", 200), ("C.WAV", 300)]
    state = {"A.WAV": 100, "B.WAV": 999}
    assert plan_downloads(remote, state) == ["B.WAV", "C.WAV"]


def test_plan_downloads_empty():
    remote = [("A.WAV", 100)]
    state = {"A.WAV": 100}
    assert plan_downloads(remote, state) == []
