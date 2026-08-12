from dayvault import app


def test_opus_recordings_sort_newest_first_with_numeric_collision_suffix():
    names = [
        "REC-20260811-1600_9_0m01s.OPUS",
        "REC-20260811-1559_0m59s.OPUS",
        "REC-20260811-1600_10_0m01s.OPUS",
    ]

    ordered = sorted(
        names,
        key=app.MainWindow._sort_key,
        reverse=True,
    )

    assert ordered == [
        "REC-20260811-1600_10_0m01s.OPUS",
        "REC-20260811-1600_9_0m01s.OPUS",
        "REC-20260811-1559_0m59s.OPUS",
    ]


def test_duration_display_comes_from_recording_name():
    assert app.MainWindow._display_duration(
        "REC-20260812-1827_3h15m55s.OPUS"
    ) == "3:15:55"
    assert app.MainWindow._display_duration(
        "REC-20260812-1012.OPUS"
    ) == "\u2014"