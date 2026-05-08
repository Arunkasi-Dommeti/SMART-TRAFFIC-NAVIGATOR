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
//   4. Add signals: clk, rst_n, rx, jA_*, jB_*, led_valid
// ============================================================

`timescale 1ns/1ps

module tb_traffic_top;

    reg  clk  = 0;
    reg  rst_n = 0;
    reg  rx   = 1;  // UART idle HIGH

    wire jA_red, jA_yellow, jA_green, jA_white;
    wire jB_red, jB_yellow, jB_green, jB_white;
    wire led_valid, led_invalid;

    // DUT
    traffic_top uut (
        .clk       (clk),
        .rst_n     (rst_n),
        .rx        (rx),
        .jA_red    (jA_red),   .jA_yellow(jA_yellow),
        .jA_green  (jA_green), .jA_white (jA_white),
        .jB_red    (jB_red),   .jB_yellow(jB_yellow),
        .jB_green  (jB_green), .jB_white (jB_white),
        .led_valid (led_valid), .led_invalid(led_invalid)
    );

    // 27 MHz clock → period = 37.037 ns ≈ 37 ns (half = 18.5 ns)
    always #18 clk = ~clk;

    // UART TX task — 9600 baud at 27 MHz
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

    // Send full 4-byte ambulance packet
    // AMB01: ID_H=0xA1, ID_L=0x01, SECRET=0x5A
    task send_emergency_packet;
        begin
            // CMD=0x31, ID_H=0xA1, ID_L=0x01
            // CHECKSUM = 0x31 ^ 0xA1 ^ 0x01 ^ 0x5A = 0xEB
            $display("[%0t] Sending EMERGENCY packet (AMB01)", $time);
            send_byte(8'h31);  // CMD
            send_byte(8'hA1);  // ID_H
            send_byte(8'h01);  // ID_L
            send_byte(8'hEB);  // CHECKSUM
            $display("[%0t] Packet sent", $time);
        end
    endtask

    task send_normal_packet;
        begin
            // CMD=0x30, CHECKSUM = 0x30 ^ 0xA1 ^ 0x01 ^ 0x5A = 0xEA
            $display("[%0t] Sending NORMAL packet (AMB01)", $time);
            send_byte(8'h30);
            send_byte(8'hA1);
            send_byte(8'h01);
            send_byte(8'hEA);
            $display("[%0t] Packet sent", $time);
        end
    endtask

    task send_spoofed_packet;
        begin
            // Wrong checksum — should be rejected
            $display("[%0t] Sending SPOOFED packet (bad checksum)", $time);
            send_byte(8'h31);
            send_byte(8'hA1);
            send_byte(8'h01);
            send_byte(8'hFF);  // Wrong checksum
            $display("[%0t] Spoof packet sent", $time);
        end
    endtask

    // ── Test sequence ──────────────────────────────────────────
    initial begin
        $dumpfile("tb_traffic.vcd");
        $dumpvars(0, tb_traffic_top);

        // Reset
        rst_n = 0; #2000;
        rst_n = 1; #2000;

        $display("=== TEST 1: Normal cycling ===");
        $display("[%0t] Jct A: RED=%b GRN=%b | Jct B: RED=%b GRN=%b",
                 $time, jA_red, jA_green, jB_red, jB_green);
        #5000000;  // Wait for some normal cycles

        $display("=== TEST 2: Valid emergency packet ===");
        send_emergency_packet();
        #500000;
        if (jA_green == 1 && jB_red == 1)
            $display("[PASS] EMERGENCY: Jct A GREEN, Jct B RED");
        else
            $display("[FAIL] EMERGENCY state not activated correctly");
        $display("[%0t] led_valid=%b", $time, led_valid);

        #3000000;

        $display("=== TEST 3: Spoofed packet (should be rejected) ===");
        send_spoofed_packet();
        #500000;
        if (jA_green == 1)
            $display("[PASS] Still in EMERGENCY (spoof rejected, state unchanged)");
        $display("[%0t] led_invalid=%b", $time, led_invalid);

        #2000000;

        $display("=== TEST 4: Normal/cancel packet ===");
        send_normal_packet();
        #500000;
        $display("[%0t] After cancel — Jct A: GREEN=%b | Jct B: GREEN=%b",
                 $time, jA_green, jB_green);

        #5000000;
        $display("=== Simulation Complete ===");
        $finish;
    end

endmodule
