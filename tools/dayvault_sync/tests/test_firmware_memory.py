import json
import re
from pathlib import Path


REPO = Path(__file__).resolve().parents[3]
LINKER = REPO / "firmware" / "boards" / "stm32l452rc.ld"
BOARD = REPO / "firmware" / "boards" / "dayvault_l452rc.json"
MAIN = REPO / "firmware" / "src" / "main.cpp"
PLATFORMIO = REPO / "firmware" / "platformio.ini"
WINUSB_DEVICE = REPO / "firmware" / "src" / "WinUsbDevice.cpp"


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


def test_usb_cdc_tx_queue_is_large_but_sram_bounded():
    text = PLATFORMIO.read_text(encoding="utf-8")
    match = re.search(
        r"CDC_TRANSMIT_QUEUE_BUFFER_PACKET_NUMBER=(\d+)",
        text,
    )
    assert match is not None
    packet_count = int(match.group(1))
    assert packet_count * 64 >= 2 * 1024
    assert packet_count * 64 <= 4 * 1024


def test_usb_benchmark_uses_sram_transfer_block_and_checked_writes():
    text = MAIN.read_text(encoding="utf-8")
    speed_handler = text[text.index('strncmp(line, "SPEED"'):]
    speed_handler = speed_handler[:speed_handler.index('strncmp(line, "BAT10"')]
    assert "transfer_buffer(1)" in speed_handler
    assert "transfer_buffer_size()" in speed_handler
    assert "serial_write_all" in speed_handler


def test_winusb_keeps_device_connected_while_host_drains_driver_buffers():
    text = MAIN.read_text(encoding="utf-8")
    match = re.search(r"kWinUsbAckTimeoutMs\s*=\s*(\d+)u", text)

    assert match is not None
    assert int(match.group(1)) >= 30_000
    assert text.count("winusb_wait_ack(kWinUsbAckTimeoutMs)") == 2


def test_winusb_keeps_data_and_trailer_in_one_contiguous_transfer():
    text = MAIN.read_text(encoding="utf-8")
    write_all = text[text.index("static bool winusb_write_all"):]
    write_all = write_all[:write_all.index("static bool winusb_wait_ack")]

    assert "kWinUsbTrailerReserve = 192u" in text
    assert re.search(
        r"kWinUsbTransferBytes\s*=\s*\n\s*"
        r"TRANSFER_BUFFER_BYTES\s*-\s*kWinUsbTrailerReserve",
        text,
    )
    assert "chunk <= 128u" not in write_all
    assert "return winusb_write_transfer(data, length);" in write_all
    assert "winusb_finish_payload" not in text

    completion = text[text.index("static bool winusb_write_completion_frame"):]
    completion = completion[:completion.index("static void download_file_bulk2")]
    assert "export_format_completion" in completion
    assert "frame_length % 64u" in completion
    assert "buffer[frame_length++] = '\\n'" in completion
    assert text.count("winusb_write_completion_frame(") == 4


def test_winusb_bulk_in_uses_reliable_single_pma_buffer():
    text = WINUSB_DEVICE.read_text(encoding="utf-8")
    in_config = re.search(
        r"HAL_PCDEx_PMAConfig\(&g_hpcd,\s*kBulkInEndpoint,\s*"
        r"(PCD_[A-Z_]+)",
        text,
    )

    assert in_config is not None
    assert in_config.group(1) == "PCD_SNG_BUF"
