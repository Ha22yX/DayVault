import pytest

from dayvault.export_protocol import Get2End, Get2Start, parse_get2_end, parse_get2_start


def test_parse_get2_start():
    assert parse_get2_start(
        "GET2START size=100000 offset=32768 length=67232"
    ) == Get2Start(total_size=100000, offset=32768, length=67232)


def test_parse_get2_start_at_eof():
    assert parse_get2_start(
        "GET2START size=4096 offset=4096 length=0"
    ) == Get2Start(total_size=4096, offset=4096, length=0)


def test_parse_get2_end():
    assert parse_get2_end(
        "GET2END sent=67232 crc32=CBF43926"
    ) == Get2End(sent=67232, crc32=0xCBF43926)


@pytest.mark.parametrize(
    "line",
    [
        "GET2START size=100 offset=101 length=0",
        "GET2START size=100 offset=20 length=79",
        "GET2START size=x offset=0 length=0",
        "GET2START size=100 offset=-1 length=101",
        "GET2 START size=100 offset=0 length=100",
    ],
)
def test_parse_get2_start_rejects_invalid_metadata(line):
    with pytest.raises(ValueError):
        parse_get2_start(line)


@pytest.mark.parametrize(
    "line",
    [
        "GET2END sent=x crc32=00000000",
        "GET2END sent=10 crc32=123",
        "GET2END sent=10 crc32=GGGGGGGG",
        "DLEND read=10",
    ],
)
def test_parse_get2_end_rejects_malformed_lines(line):
    with pytest.raises(ValueError):
        parse_get2_end(line)
