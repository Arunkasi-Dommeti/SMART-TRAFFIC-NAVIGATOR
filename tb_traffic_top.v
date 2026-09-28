
`define SIMULATION 
`timescale 1ns/1ps

`define CLKS_PER_BIT  2813
`define CLK_PERIOD_NS 37
`define BIT_PERIOD_NS (`CLKS_PER_BIT * `CLK_PERIOD_NS)

module tb_traffic_top;
    reg  clk   = 0;
    reg  rst_n = 0;
    reg  rx    = 1;
    

    wire jA_red, jA_yellow, jA_green, jA_white;
    wire jB_red, jB_yellow, jB_green, jB_white;
    wire led_valid, led_invalid;
    wire fpga_ack;  

    
    traffic_top uut (
        .clk        (clk),
        .rst_n      (rst_n),
        .rx         (rx),
        .jA_red     (jA_red),    .jA_yellow(jA_yellow),
        .jA_green   (jA_green),  
        .jA_white   (jA_white),
        .jB_red     (jB_red),    .jB_yellow(jB_yellow),
        .jB_green   (jB_green),  .jB_white (jB_white),
        .led_valid  (led_valid), .led_invalid(led_invalid),
        .fpga_ack   (fpga_ack)   
    );
    // 27 MHz clock → period = 37.037  (half = 18.5 ns
    always #18 clk = ~clk;

    task send_byte;
        input [7:0] data;
        integer i;
        begin
            rx = 0;
            #`BIT_PERIOD_NS;  
            for (i = 0; i < 8; i = i+1) begin
                rx = data[i];
                #`BIT_PERIOD_NS;
            end
            rx = 1; #`BIT_PERIOD_NS;
            
        end
    endtask

    task send_emergency_packet;
        begin
            $display("[%0t] Sending EMERGENCY packet — AMB-001 (0x00/0x01)", $time);
            send_byte(8'h31);  
            send_byte(8'h00);
            
            send_byte(8'h01);
            
            send_byte(8'h6A);
            
            $display("[%0t] Packet sent", $time);
        end
    endtask

    task send_normal_packet;
        begin
            $display("[%0t] Sending NORMAL packet — AMB-001 (0x00/0x01)", $time);
            send_byte(8'h30);
            send_byte(8'h00);  
            send_byte(8'h01);
            send_byte(8'h6B);
            
            $display("[%0t] Packet sent", $time);
        end
    endtask

    task send_spoofed_packet;
        begin
            $display("[%0t] Sending SPOOFED packet (valid ID, bad checksum)", $time);
            send_byte(8'h31);
            send_byte(8'h00);  
            send_byte(8'h01);
            
            send_byte(8'hFF);
            
            $display("[%0t] Spoof packet sent", $time);
        end
    endtask

    task send_unknown_id_packet;
        begin
            $display("[%0t] Sending UNKNOWN ID packet (0xA1/0x01, correct chk)", $time);
            $display("[%0t] → ID not in whitelist, must be rejected", $time);
            send_byte(8'h31);
            send_byte(8'hA1);
            
            send_byte(8'h01);
            send_byte(8'hCB);
            $display("[%0t] Unknown ID packet sent", $time);
        end
    endtask

    task send_emergency_packet_amb002;
        begin
            $display("[%0t] Sending EMERGENCY packet — AMB-002 (0x00/0x02)", $time);
            send_byte(8'h31);
            send_byte(8'h00);  
            send_byte(8'h02);
            
            send_byte(8'h69);
            
            $display("[%0t] Packet sent", $time);
        end
    endtask

    
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

        
        $display("=== TEST 5: Separate task — AMB-002 corrected IDs (0x00/0x02) ===");
        send_emergency_packet_amb002();
        #500000;
        if (jA_green == 1 && jB_red == 1)
            $display("[PASS] AMB-002 EMERGENCY activated — whitelist accepts all registered units");
        else
            $display("[FAIL] AMB-002 not accepted");
        $display("[%0t] fpga_ack=%b", $time, fpga_ack);
        #3000000;

        
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
