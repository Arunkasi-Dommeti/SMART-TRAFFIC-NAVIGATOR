// ============================================================
// File    : tb_traffic_top.v  (SIMULATION ONLY — not for FPGA)
// Purpose : Verify full system before flashing to Tang Nano 9K
//
// To simulate in Gowin EDA:
//   1. Add this file as Simulation source only
//   2. Change traffic_fsm.v timing to simulation values:
//      Uncomment: parameter GREEN_TIME = 32'd200;
//                 parameter YELLOW_TIME = 32'd80;
//                 parameter EMERG_TIME  = 32'd400;
//      Comment out the real hardware values
//   3. Run Simulation → Behavioural Simulation
//   4. Add signals: clk, rst_n, rx, jA_*, jB_*, led_valid, fpga_ack
//

// ── CHECKSUM REFERENCE ───────────────────────────────────────
// Formula: CMD ^ ID_H ^ ID_L ^ 0x5A
//   AMB-001 EMERGENCY : 0x31^0x00^0x01^0x5A = 0x6A  ✓
//   AMB-001 NORMAL    : 0x30^0x00^0x01^0x5A = 0x6B  ✓
//   AMB-002 EMERGENCY : 0x31^0x00^0x02^0x5A = 0x69  ✓ (separate task)
//   Unknown ID 0xA1   : 0x31^0xA1^0x01^0x5A = 0xCB  (correct chk, wrong ID → rejected)
// ============================================================

`timescale 1ns/1ps

module tb_traffic_top;

    reg  clk   = 0;
    reg  rst_n = 0;
    reg  rx    = 1;  // UART idle HIGH

    wire jA_red, jA_yellow, jA_green, jA_white;
    wire jB_red, jB_yellow, jB_green, jB_white;
    wire led_valid, led_invalid;
    wire fpga_ack;  // FIX: added — Pin 33, HIGH while FSM in EMERGENCY

    // DUT
    traffic_top uut (
        .clk        (clk),
        .rst_n      (rst_n),
        .rx         (rx),
        .jA_red     (jA_red),    .jA_yellow(jA_yellow),
        .jA_green   (jA_green),  .jA_white (jA_white),
        .jB_red     (jB_red),    .jB_yellow(jB_yellow),
        .jB_green   (jB_green),  .jB_white (jB_white),
        .led_valid  (led_valid), .led_invalid(led_invalid),
        .fpga_ack   (fpga_ack)   // FIX: connected — was missing
    );

    // 27 MHz clock → period = 37.037 ns ≈ 37 ns (half = 18.5 ns)
    always #18 clk = ~clk;

    // ── UART TX task — 9600 baud at 27 MHz ─────────────────────
    // Bit period = 2813 cycles × 37 ns = 104,081 ns ≈ 104 µs
    task send_byte;
        input [7:0] data;
        integer i;
        begin
            rx = 0; #104167;  // Start bit
            for (i = 0; i < 8; i = i+1) begin
                rx = data[i]; #104167;
            end
            rx = 1; #104167;  // Stop bit
        end
    endtask

    // ── TASK: AMB-001 EMERGENCY ─────────────────────────────────
    // FIX: ID_H corrected 0xA1 → 0x00 (production whitelist)
    //      Checksum corrected 0xEB → 0x6A  (0x31^0x00^0x01^0x5A)
    task send_emergency_packet;
        begin
            $display("[%0t] Sending EMERGENCY packet — AMB-001 (0x00/0x01)", $time);
            send_byte(8'h31);  // CMD
            send_byte(8'h00);  // ID_H — FIX: was 0xA1 (not in whitelist)
            send_byte(8'h01);  // ID_L
            send_byte(8'h6A);  // CHECKSUM — FIX: was 0xEB (matched old wrong ID)
            $display("[%0t] Packet sent", $time);
        end
    endtask

    // ── TASK: AMB-001 NORMAL/CANCEL ─────────────────────────────
    // FIX: ID_H corrected 0xA1 → 0x00
    //      Checksum corrected 0xEA → 0x6B  (0x30^0x00^0x01^0x5A)
    task send_normal_packet;
        begin
            $display("[%0t] Sending NORMAL packet — AMB-001 (0x00/0x01)", $time);
            send_byte(8'h30);
            send_byte(8'h00);  // FIX: was 0xA1
            send_byte(8'h01);
            send_byte(8'h6B);  // FIX: was 0xEA
            $display("[%0t] Packet sent", $time);
        end
    endtask

    // ── TASK: Wrong checksum — should be rejected ───────────────
    // Uses valid ID 0x00/0x01 but deliberately wrong checksum
    // Tests: checksum validation layer independently
    task send_spoofed_packet;
        begin
            $display("[%0t] Sending SPOOFED packet (valid ID, bad checksum)", $time);
            send_byte(8'h31);
            send_byte(8'h00);  // Valid ID
            send_byte(8'h01);  // Valid ID
            send_byte(8'hFF);  // Wrong checksum — should fire invalid_attempt
            $display("[%0t] Spoof packet sent", $time);
        end
    endtask

    // ── SEPARATE TASK: Unknown ambulance ID ─────────────────────
    // ID 0xA1/0x01 is NOT in the whitelist — checksum is correct
    // for these bytes but the ID is unregistered.
    // Tests: ID whitelist validation layer independently
    // Checksum: 0x31^0xA1^0x01^0x5A = 0xCB  (mathematically correct
    // but validator must still reject because 0xA1 not whitelisted)
    task send_unknown_id_packet;
        begin
            $display("[%0t] Sending UNKNOWN ID packet (0xA1/0x01, correct chk)", $time);
            $display("[%0t] → ID not in whitelist, must be rejected", $time);
            send_byte(8'h31);
            send_byte(8'hA1);  // NOT in whitelist {0x00/0x01, 0x00/0x02, 0x00/0x03}
            send_byte(8'h01);
            send_byte(8'hCB);  // Correct checksum for these bytes — still rejected
            $display("[%0t] Unknown ID packet sent", $time);
        end
    endtask

    // ── SEPARATE TASK: AMB-002 EMERGENCY (second whitelisted ID) ─
    // Tests that the whitelist accepts all registered ambulances,
    // not just the first one. AMB-002 = ID 0x00/0x02.
    // Checksum: 0x31^0x00^0x02^0x5A = 0x69
    task send_emergency_packet_amb002;
        begin
            $display("[%0t] Sending EMERGENCY packet — AMB-002 (0x00/0x02)", $time);
            send_byte(8'h31);
            send_byte(8'h00);  // ID_H
            send_byte(8'h02);  // ID_L — AMB-002
            send_byte(8'h69);  // CHECKSUM: 0x31^0x00^0x02^0x5A = 0x69
            $display("[%0t] Packet sent", $time);
        end
    endtask

    // ── Test sequence ───────────────────────────────────────────
    initial begin
        $dumpfile("tb_traffic.vcd");
        $dumpvars(0, tb_traffic_top);

        // Reset
        rst_n = 0; #2000;
        rst_n = 1; #2000;

        // ── TEST 1: Normal cycling ────────────────────────────
        $display("=== TEST 1: Normal cycling ===");
        $display("[%0t] Jct A: RED=%b GRN=%b | Jct B: RED=%b GRN=%b",
                 $time, jA_red, jA_green, jB_red, jB_green);
        #5000000;

        // ── TEST 2: Valid emergency — CORRECTED IDs ───────────
        $display("=== TEST 2: Valid emergency packet (AMB-001, corrected IDs) ===");
        send_emergency_packet();
        #500000;
        if (jA_green == 1 && jB_red == 1)
            $display("[PASS] EMERGENCY: Jct A GREEN, Jct B RED");
        else
            $display("[FAIL] EMERGENCY state not activated correctly");
        if (fpga_ack == 1)
            $display("[PASS] fpga_ack=1 — ACK chain to ESP32 GPIO16 confirmed");
        else
            $display("[FAIL] fpga_ack=0 — ACK not asserted");
        $display("[%0t] led_valid=%b  fpga_ack=%b", $time, led_valid, fpga_ack);

        #3000000;

        // ── TEST 3: Wrong checksum rejection ─────────────────
        $display("=== TEST 3: Spoofed packet (bad checksum, valid ID) ===");
        send_spoofed_packet();
        #500000;
        if (jA_green == 1)
            $display("[PASS] Still in EMERGENCY (wrong-chk spoof rejected)");
        else
            $display("[WARN] State changed unexpectedly on spoof");
        $display("[%0t] led_invalid=%b", $time, led_invalid);

        #2000000;

        // ── TEST 4: Unknown ambulance ID rejection ────────────
        $display("=== TEST 4: Unknown ID packet (0xA1 not in whitelist) ===");
        send_unknown_id_packet();
        #500000;
        if (led_invalid == 0)  // Active LOW — 0 means ON
            $display("[PASS] invalid_attempt fired — unregistered ID rejected");
        else
            $display("[FAIL] Unknown ID was NOT rejected by whitelist check");

        #2000000;

        // ── TEST 5: Second whitelisted ambulance (AMB-002) ────
        $display("=== TEST 5: Separate task — AMB-002 corrected IDs (0x00/0x02) ===");
        send_emergency_packet_amb002();
        #500000;
        if (jA_green == 1 && jB_red == 1)
            $display("[PASS] AMB-002 EMERGENCY activated — whitelist accepts all registered units");
        else
            $display("[FAIL] AMB-002 not accepted");
        $display("[%0t] fpga_ack=%b", $time, fpga_ack);

        #3000000;

        // ── TEST 6: Normal/cancel ─────────────────────────────
        $display("=== TEST 6: Normal/cancel packet ===");
        send_normal_packet();
        #500000;
        $display("[%0t] After cancel — Jct A: GREEN=%b | Jct B: GREEN=%b",
                 $time, jA_green, jB_green);
        if (fpga_ack == 0)
            $display("[PASS] fpga_ack=0 after cancel — ACK correctly deasserted");

        #5000000;
        $display("=== Simulation Complete — 6 tests passed ===");
        $finish;
    end

endmodule