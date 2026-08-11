import json
import re
from pathlib import Path


REPO = Path(__file__).resolve().parents[3]
LINKER = REPO / "firmware" / "boards" / "stm32l452rc.ld"
BOARD = REPO / "firmware" / "boards" / "dayvault_l452rc.json"
MAIN = REPO / "firmware" / "src" / "main.cpp"
CONFIG = REPO / "firmware" / "src" / "Config.h"
COMPRESSED_RECORDER = REPO / "firmware" / "src" / "CompressedRecorder.cpp"
COMPRESSED_RECORDER_HEADER = REPO / "firmware" / "src" / "CompressedRecorder.h"
FS = REPO / "firmware" / "src" / "Fs.cpp"
PLATFORMIO = REPO / "firmware" / "platformio.ini"
OPUS_LIBRARY = REPO / "firmware" / "lib" / "opus" / "library.json"
WINUSB_DEVICE = REPO / "firmware" / "src" / "WinUsbDevice.cpp"
PDM_CAPTURE = REPO / "firmware" / "src" / "PdmCapture.cpp"
PDM_CAPTURE_HEADER = REPO / "firmware" / "src" / "PdmCapture.h"


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


def test_release_build_uses_size_optimization_except_for_opus():
    platformio_text = PLATFORMIO.read_text(encoding="utf-8")
    opus_library = json.loads(OPUS_LIBRARY.read_text(encoding="utf-8"))
    assert re.search(r"build_flags\s*=\s*\n\s*-Os", platformio_text)
    assert "-O2" in opus_library["build"]["flags"]


def test_main_has_no_16k_transfer_buffer_on_stack():
    text = MAIN.read_text(encoding="utf-8")
    assert not re.search(r"uint8_t\s+\w+\s*\[\s*16384\s*\]", text)


def test_production_recorder_uses_direct_paired_ogg_opus_path():
    recorder_text = (
        COMPRESSED_RECORDER_HEADER.read_text(encoding="utf-8")
        + "\n"
        + COMPRESSED_RECORDER.read_text(encoding="utf-8")
        if COMPRESSED_RECORDER.exists()
        else ""
    )
    config_text = CONFIG.read_text(encoding="utf-8")
    main_text = MAIN.read_text(encoding="utf-8")
    all_firmware_text = "\n".join(
        path.read_text(encoding="utf-8", errors="ignore")
        for path in (REPO / "firmware" / "src").rglob("*")
        if path.is_file()
    )

    assert "pdm_dma_read_dual" in recorder_text
    assert "AudioPipeline" in recorder_text
    assert "DayVaultOpusEncoder" in recorder_text
    assert "OggOpusWriter" in recorder_text
    assert 'REC_EXT_STR "OPUS"' in config_text
    assert "wav_build_header" not in recorder_text
    assert "OPUSSTAT" in main_text
    assert "0x1FFF0000u" in main_text
    assert "0x1FFF7800" not in all_firmware_text


def test_production_codec_is_linked_without_temporary_probe():
    platformio_text = PLATFORMIO.read_text(encoding="utf-8")
    encoder_text = (REPO / "firmware" / "src" / "DayVaultOpusEncoder.cpp").read_text(
        encoding="utf-8"
    )

    assert "dayvault_opus_encoder_link_probe" not in platformio_text
    assert "dayvault_opus_encoder_link_probe" not in encoder_text


def test_pdm_stop_freezes_exact_paired_producer_snapshot_before_dma_clear():
    text = PDM_CAPTURE.read_text(encoding="utf-8")
    header = PDM_CAPTURE_HEADER.read_text(encoding="utf-8")
    stop = text[text.index("void pdm_stop_and_freeze(void)") :]
    stop = stop[: stop.index("static int16_t pdm_scale_raw")]
    read = text[text.index("int pdm_dma_read_dual") :]
    read = read[: read.index("int pdm_dma_read(")]

    assert "pdm_stop_and_freeze" in header
    assert "pdm_dma_frozen" in text
    assert stop.count("pdm_dma_snapshot_produced(") == 2
    first_snapshot = stop.index("pdm_dma_snapshot_produced(")
    assert first_snapshot < stop.index("PDM_DMA_CH->CCR &= ~DMA_CCR_EN_Msk")
    assert first_snapshot < stop.index("DMA1->IFCR = DMA_IFCR_CTCIF4 | DMA_IFCR_CTCIF5")
    assert "pdm_ring_recover_frozen_pair" in read


def test_failed_start_cleanup_reports_errors_and_preserves_retry_state():
    text = COMPRESSED_RECORDER.read_text(encoding="utf-8")
    header = COMPRESSED_RECORDER_HEADER.read_text(encoding="utf-8")
    start = text[text.index("CompressedRecorderResult CompressedRecorder::start") :]
    start = start[: start.index("CompressedRecorderResult CompressedRecorder::poll")]
    cleanup = text[text.index("CompressedRecorder::cleanup_partial_file") :]
    cleanup = cleanup[: cleanup.index("void CompressedRecorder::set_result")]

    assert "COMPRESSED_RECORDER_ERR_CLEANUP_CLOSE" in header
    assert "COMPRESSED_RECORDER_ERR_CLEANUP_UNLINK" in header
    assert "primary_result" in header
    assert "cleanup_result" in header
    assert start.index("cleanup_partial_file()") < start.index("memset(&stats_")
    assert "f_close(&file_)" in cleanup
    assert "f_unlink(name_)" in cleanup
    assert re.search(r"f_close\(&file_\).*?file_open_\s*=\s*false", cleanup, re.DOTALL)
    assert re.search(r"f_unlink\(name_\).*?file_created_\s*=\s*false", cleanup, re.DOTALL)
    assert "cleanup_pending_ = file_open_ || file_created_" in cleanup

    stop = text[text.index("CompressedRecorderResult CompressedRecorder::stop") :]
    stop = stop[: stop.index("bool CompressedRecorder::active")]
    assert "if (close_result == FR_OK)" in stop
    assert stop.index("if (close_result == FR_OK)") < stop.index("file_open_ = false")

    main = MAIN.read_text(encoding="utf-8")
    opusstat = main[main.index("static void opusstat_print") :]
    opusstat = opusstat[: opusstat.index("static void exti_usb_wake_enable")]
    assert "stats.primary_result" in opusstat
    assert "stats.cleanup_result" in opusstat


def test_duration_rename_searches_collision_free_destination_from_stable_stem():
    text = COMPRESSED_RECORDER.read_text(encoding="utf-8")
    header = COMPRESSED_RECORDER_HEADER.read_text(encoding="utf-8")
    rename = text[text.index("bool CompressedRecorder::rename_with_duration") :]

    assert "timestamp_stem_" in header
    assert "recording_format_timestamp_path" in rename
    assert "f_stat" in rename
    assert "FR_NO_FILE" in rename
    assert rename.index("f_stat") < rename.index("f_rename")


def test_all_pdm_diagnostics_exclude_recording_and_service_watchdog():
    text = MAIN.read_text(encoding="utf-8")
    commands = {
        "DMAT": 'strncmp(line, "DMAT", 4)',
        "ITST": 'strncmp(line, "ITST", 4)',
        "SAMP": 'strncmp(line, "SAMP", 4)',
        "CAPT": 'strncmp(line, "CAPT", 4)',
        "DSCAN": 'strncmp(line, "DSCAN", 5)',
        "NF": 'strncmp(line, "NF", 2)',
        "DUAL": 'strncmp(line, "DUAL", 4)',
        "RAW": 'strncmp(line, "RAW", 3)',
    }
    for name, marker in commands.items():
        start = text.index(marker)
        end = text.find("} else if", start)
        block = text[start : end if end >= 0 else len(text)]
        assert "rec_stop()" in block, f"{name} must release production PDM ownership"

    for name in ("DMAT", "ITST", "DSCAN", "NF", "DUAL", "RAW"):
        start = text.index(commands[name])
        end = text.find("} else if", start)
        block = text[start : end if end >= 0 else len(text)]
        assert "dbg_iwdg_kick()" in block, f"{name} blocking loop must kick IWDG"

    capture = text[text.index("static void record_test") :]
    capture = capture[: capture.index("static void sample_stats")]
    sample = text[text.index("static void sample_stats") :]
    sample = sample[: sample.index("static void download_file")]
    assert "dbg_iwdg_kick()" in capture
    assert "dbg_iwdg_kick()" in sample


def test_recording_deletion_keeps_legacy_wav_compatibility():
    text = FS.read_text(encoding="utf-8")

    assert "is_recording_extension" in text
    assert "REC_EXT_STR" in text
    assert '"WAV"' in text


def test_sram2_transfer_commands_exclude_production_recording():
    text = MAIN.read_text(encoding="utf-8")
    command_handler = text[text.index('strcmp(line, "BULKSPEED")'):]
    command_handler = command_handler[: command_handler.index('strncmp(line, "BAT10"')]

    assert "transfer_workspace_busy" in text
    for command in ('"BULKSPEED"', '"BULK2 "', '"GET2 "', '"DL2 "', '"SDSPEED"', '"SPEED"'):
        start = command_handler.index(command)
        block = command_handler[start : start + 900]
        assert "rec_stop()" in block
        assert "transfer_workspace_busy = true" in block


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
