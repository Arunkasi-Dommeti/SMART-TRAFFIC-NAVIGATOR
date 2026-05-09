// ============================================================
// Module  : packet_validator.v
// Board   : Tang Nano 9K
//
// Function:
//   Receives 4 bytes from uart_rx and validates the packet.
//   Packet format (from gateway ESP32):
//     Byte 0: CMD      = 0x31 (emergency) or 0x30 (normal)
//     Byte 1: AMB_ID_H = high byte of ambulance ID
//     Byte 2: AMB_ID_L = low  byte of ambulance ID
//     Byte 3: CHECKSUM = CMD ^ ID_H ^ ID_L ^ 0x5A
//
//   Validation steps:
//     1. CMD must be 0x31 or 0x30
//     2. Checksum must equal CMD ^ ID_H ^ ID_L ^ SECRET_KEY
//     3. Ambulance ID must be in hardcoded whitelist
//
//   Outputs:
//     emergency_out  : 1 = valid emergency, 0 = valid normal/cancel
//     valid_packet   : 1-cycle pulse on valid packet
//     invalid_attempt: 1-cycle pulse on spoofed/bad packet
// ============================================================

module packet_validator (
    input  wire       clk,
    input  wire       rst_n,
    input  wire       data_valid,    // From uart_rx
    input  wire [7:0] rx_data,       // From uart_rx

    output reg        emergency_out,
    output reg        valid_packet,
    output reg        invalid_attempt
);

    // ── Shared secret key — must match gateway ESP32 ──────────
    parameter SECRET_KEY = 8'h5A;

    // ── 4-byte assembly states ─────────────────────────────────
    parameter WAIT_CMD  = 2'd0;
    parameter WAIT_ID_H = 2'd1;
    parameter WAIT_ID_L = 2'd2;
    parameter WAIT_CHK  = 2'd3;

    reg [1:0] state;
    reg [7:0] r_cmd, r_id_h, r_id_l;

    // ── Whitelist: registered ambulance IDs ───────────────────
    // {ID_H, ID_L} pairs — must match gateway ESP32 WHITELIST
    // and ambulance firmware AMBULANCE_NUM_ID definitions.
    //
    // Gateway whitelist:  { 0x0001, 0x0002, 0x0003 }
    // Ambulance firmware: AMBULANCE_NUM_ID = 0x0001
    //   → ID_H = 0x00, ID_L = 0x01
    //
    // ⚠️  BUG FIX (2026-05): Previous whitelist used 0xA1xx/0xA2xx IDs
    //   which did NOT match the actual ambulance firmware (0x0001).
    //   This caused every real ambulance packet to fire invalid_attempt
    //   and the FSM never entered EMERGENCY state on hardware.
    //   Fixed by aligning with gateway ESP32 whitelist.
    function automatic is_whitelisted;
        input [7:0] h, l;
        begin
            is_whitelisted =
                (h == 8'h00 && l == 8'h01) ||  // AMB-001 (ambulance-module.ino default)
                (h == 8'h00 && l == 8'h02) ||  // AMB-002
                (h == 8'h00 && l == 8'h03);    // AMB-003
        end
    endfunction

    always @(posedge clk) begin
        if (!rst_n) begin
            state          <= WAIT_CMD;
            emergency_out  <= 1'b0;
            valid_packet   <= 1'b0;
            invalid_attempt<= 1'b0;
            r_cmd  <= 8'h0;
            r_id_h <= 8'h0;
            r_id_l <= 8'h0;
        end else begin
            // Pulse outputs for 1 cycle only
            valid_packet    <= 1'b0;
            invalid_attempt <= 1'b0;

            if (data_valid) begin
                case (state)

                    WAIT_CMD: begin
                        // Only accept valid command bytes
                        if (rx_data == 8'h31 || rx_data == 8'h30) begin
                            r_cmd <= rx_data;
                            state <= WAIT_ID_H;
                        end
                        // Any other byte: ignore, wait for next frame
                    end

                    WAIT_ID_H: begin
                        r_id_h <= rx_data;
                        state  <= WAIT_ID_L;
                    end

                    WAIT_ID_L: begin
                        r_id_l <= rx_data;
                        state  <= WAIT_CHK;
                    end

                    WAIT_CHK: begin
                        // Compute expected checksum
                        // Note: using reg for combinational — synthesis safe
                        begin
                            reg [7:0] expected_chk;
                            expected_chk = r_cmd ^ r_id_h ^ r_id_l ^ SECRET_KEY;

                            if (rx_data == expected_chk &&
                                is_whitelisted(r_id_h, r_id_l)) begin
                                // VALID PACKET
                                emergency_out <= (r_cmd == 8'h31) ? 1'b1 : 1'b0;
                                valid_packet  <= 1'b1;
                            end else begin
                                // INVALID — spoofed or unknown ID
                                invalid_attempt <= 1'b1;
                                // Do NOT change emergency_out on invalid
                            end
                        end
                        state <= WAIT_CMD;  // Always reset to wait next packet
                    end

                endcase
            end
        end
    end

endmodule
