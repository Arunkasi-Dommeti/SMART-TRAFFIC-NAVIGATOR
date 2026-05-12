import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def read_file(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8")


def arduino_define(source: str, name: str) -> int:
    match = re.search(rf"^\s*#define\s+{name}\s+(0x[0-9A-Fa-f]+|\d+)", source, re.MULTILINE)
    assert match, f"Missing Arduino define: {name}"
    return int(match.group(1), 0)


def verilog_parameter(source: str, name: str) -> int:
    match = re.search(rf"\bparameter\s+{name}\s*=\s*8'h([0-9A-Fa-f]+)", source)
    assert match, f"Missing Verilog parameter: {name}"
    return int(match.group(1), 16)


def gateway_whitelist(source: str) -> set[int]:
    match = re.search(r"ID_WHITELIST\[\]\s*=\s*\{([^}]+)\}", source)
    assert match, "Missing gateway ID whitelist"
    return {int(value, 0) for value in re.findall(r"0x[0-9A-Fa-f]+|\d+", match.group(1))}


def fpga_whitelist(source: str) -> set[int]:
    pairs = re.findall(r"h\s*==\s*8'h([0-9A-Fa-f]{2})\s*&&\s*l\s*==\s*8'h([0-9A-Fa-f]{2})", source)
    assert pairs, "Missing FPGA packet_validator whitelist"
    return {(int(high, 16) << 8) | int(low, 16) for high, low in pairs}


def encode_packet(cmd: int, ambulance_id: int, secret: int) -> list[int]:
    id_h = (ambulance_id >> 8) & 0xFF
    id_l = ambulance_id & 0xFF
    return [cmd, id_h, id_l, cmd ^ id_h ^ id_l ^ secret]


def validate_packet(packet: list[int], secret: int, whitelist: set[int], valid_commands: set[int]) -> dict:
    cmd, id_h, id_l, checksum = packet
    ambulance_id = (id_h << 8) | id_l
    expected_checksum = cmd ^ id_h ^ id_l ^ secret
    valid = (
        cmd in valid_commands
        and checksum == expected_checksum
        and ambulance_id in whitelist
    )
    return {
        "valid": valid,
        "emergency": valid and cmd == 0x31,
        "ambulance_id": ambulance_id,
        "expected_checksum": expected_checksum,
    }


def test_lora_to_gateway_to_fpga_packet_contract_matches_across_sources():
    ambulance = read_file("ambulance-module.ino")
    gateway = read_file("gate-way_.ino")
    fpga = read_file("packet_validator.v")

    ambulance_id = arduino_define(ambulance, "AMBULANCE_NUM_ID")
    ambulance_secret = arduino_define(ambulance, "XOR_SECRET_KEY")
    gateway_secret = arduino_define(gateway, "XOR_SECRET_KEY")
    fpga_secret = verilog_parameter(fpga, "SECRET_KEY")
    cmd_emergency = arduino_define(ambulance, "CMD_EMERGENCY")
    cmd_normal = arduino_define(ambulance, "CMD_NORMAL")

    assert cmd_emergency == arduino_define(gateway, "CMD_EMERGENCY") == 0x31
    assert cmd_normal == arduino_define(gateway, "CMD_NORMAL") == 0x30
    assert ambulance_secret == gateway_secret == fpga_secret == 0x5A
    assert ambulance_id in gateway_whitelist(gateway)
    assert ambulance_id in fpga_whitelist(fpga)

    packet = encode_packet(cmd_emergency, ambulance_id, ambulance_secret)
    gateway_result = validate_packet(packet, gateway_secret, gateway_whitelist(gateway), {cmd_emergency, cmd_normal})
    fpga_result = validate_packet(packet, fpga_secret, fpga_whitelist(fpga), {cmd_emergency, cmd_normal})

    assert packet == [0x31, 0x00, 0x01, 0x6A]
    assert gateway_result == {
        "valid": True,
        "emergency": True,
        "ambulance_id": 0x0001,
        "expected_checksum": 0x6A,
    }
    assert fpga_result == gateway_result


def test_gateway_normal_cancel_uses_last_valid_ambulance_id_for_fpga():
    gateway = read_file("gate-way_.ino")
    gateway_code = "\n".join(
        line for line in gateway.splitlines()
        if not line.lstrip().startswith("//")
    )
    normal_forward_pattern = re.compile(
        r"forwardToFPGA\(\s*CMD_NORMAL,\s*\(activeAmbId\s*>>\s*8\)\s*&\s*0xFF,\s*activeAmbId\s*&\s*0xFF\)",
        re.DOTALL,
    )
    matches = normal_forward_pattern.findall(gateway_code)

    assert len(matches) >= 2, "Cancel and timeout paths must forward a whitelisted activeAmbId"
    assert "forwardToFPGA(CMD_NORMAL, 0x00, 0x00)" not in gateway_code


def test_firebase_stream_trigger_contract_activates_ambulance_lora_path():
    ambulance = read_file("ambulance-module.ino")

    assert '"/ambulances/" + String(AMBULANCE_ID) + "/emergency_id"' in ambulance
    assert "Firebase.RTDB.beginStream(&stream, path.c_str())" in ambulance
    assert "Firebase.RTDB.setStreamCallback(&stream, onEmergencyTriggered, onStreamTimeout)" in ambulance
    assert "currentEmergencyId = emergencyId;" in ambulance
    assert "isEmergencyActive = true;" in ambulance
    assert 'updateAmbulanceStatus("responding")' in ambulance
    assert "transmitLoRa();" in ambulance

    def is_valid_emergency_id(value: str) -> bool:
        return len(value) > 5 and value not in {"null", "waiting", "idle", "0", ""}

    assert is_valid_emergency_id("EMG-2026-0001")
    assert is_valid_emergency_id("MANUAL-123456")
    for invalid in ("", "0", "idle", "null", "wait"):
        assert not is_valid_emergency_id(invalid)


def test_ambulance_gps_publishes_only_fresh_neo6m_fixes():
    ambulance = read_file("ambulance-module.ino")

    assert "gpsFixIsFresh()" in ambulance
    assert "GPS_MAX_FIX_AGE_MS" in ambulance
    assert "float currentLat = 17.3850;" not in ambulance
    assert "float currentLng = 78.4867;" not in ambulance
    assert 'json.set("gps_fix", hasGpsFix);' in ambulance
    assert 'json.set("source", "neo-6m");' in ambulance
    assert re.search(r"if\s*\(hasGpsFix\)\s*\{[^}]*json\.set\(\"lat\", currentLat\);", ambulance, re.DOTALL)
    assert re.search(r"if\s*\(hasGpsFix\)\s*\{[^}]*json\.set\(\"lat\", currentLat\);[^}]*json\.set\(\"lng\", currentLng\);", ambulance, re.DOTALL)


def test_emt_interface_uses_backend_ai_proxy_and_public_config_allowlist():
    html = read_file("EMT_interface.html")

    assert "generativelanguage.googleapis.com" not in html
    assert "window.GEMINI_KEY" not in html
    assert "GEMINI_API_KEY" not in html
    assert "/api/v1/ai/first-aid" in html
    assert "PUBLIC_ENV_KEYS" in html
    assert "window.__SMART_TRAFFIC_CONFIG__" in html
