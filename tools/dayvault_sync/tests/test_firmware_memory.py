import json
import re
from pathlib import Path


REPO = Path(__file__).resolve().parents[3]
LINKER = REPO / "firmware" / "boards" / "stm32l452rc.ld"
BOARD = REPO / "firmware" / "boards" / "dayvault_l452rc.json"
MAIN = REPO / "firmware" / "src" / "main.cpp"


def test_linker_exposes_all_stm32l452_sram():
    text = LINKER.read_text(encoding="utf-8")
    assert re.search(
        r"RAM\s+\(xrw\)\s*:\s*ORIGIN\s*=\s*0x20000000,\s*LENGTH\s*=\s*128K",
        text,
    )
    assert re.search(
        r"RAM2\s+\(xrw\)\s*:\s*ORIGIN\s*=\s*0x10000000,\s*LENGTH\s*=\s*32K",
        text,
    )


def test_linker_has_dedicated_noload_ram2_section():
    text = LINKER.read_text(encoding="utf-8")
    assert re.search(r"\.ram2\s*\(NOLOAD\)\s*:", text)
    assert re.search(r"__ram2_start__.*__ram2_end__.*>RAM2", text, re.DOTALL)


def test_platformio_board_reports_total_sram():
    board = json.loads(BOARD.read_text(encoding="utf-8"))
    assert board["upload"]["maximum_ram_size"] == 160 * 1024


def test_main_has_no_16k_transfer_buffer_on_stack():
    text = MAIN.read_text(encoding="utf-8")
    assert not re.search(r"uint8_t\s+\w+\s*\[\s*16384\s*\]", text)
