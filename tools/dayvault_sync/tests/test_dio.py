from dayvault.dio import list_dayvault_ports, _VIDPID_RE, _SER_RE


def test_vidpid_regex():
    hw = "USB VID:PID=0483:5740 SER=206C36943831"
    m = _VIDPID_RE.search(hw)
    assert m and int(m.group(1), 16) == 0x0483 and int(m.group(2), 16) == 0x5740
    assert _SER_RE.search(hw).group(1) == "206C36943831"


def test_other_vidpid_rejected():
    assert not _VIDPID_RE.search("USB VID:PID=1D6B:0002").group(2) == "5740"
