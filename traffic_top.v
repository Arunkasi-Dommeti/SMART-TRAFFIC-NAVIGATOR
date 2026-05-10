// ============================================================
// Module  : traffic_top.v  (TOP MODULE)
// Board   : Tang Nano 9K (Gowin GW1NR-9, 27 MHz)
// Project : Smart Traffic Navigator — 2 Junction Controller
//
// Instantiates:
//   uart_rx          → receives bytes from ESP32 gateway
//   packet_validator → validates 4-byte secured packet
//   traffic_fsm      → 6-state FSM controls LED outputs
//
// Port → Physical Pin mapping (see smart_traffic.cst):
//   clk       → Pin 52   (27 MHz oscillator)
//   rst_n     → Pin 4    (S1 button, active LOW)
//   rx        → Pin 40   (IOB33B, from ESP32 GPIO17)
//   jA_*      → Pins 25-28 (Junction A LEDs)
//   jB_*      → Pins 29-32 (Junction B LEDs)
//   led_valid → Pin 10   (Onboard LED1 — valid packet indicator)
//   led_invalid→ Pin 11  (Onboard LED2 — spoof attempt indicator)
//  fpga_ack  → Pin 33   (EMERGENCY ACK → ESP32 GPIO16)
//
//   All output pins are BANK2 (3.3V LVCMOS33)
//   Onboard LED pins are BANK3 (1.8V LVCMOS18) — see .cst file
//   NEVER apply 5V to any Tang Nano 9K GPIO pin
//   Power via USB-C connector ONLY
// ============================================================

module traffic_top (
    input  wire clk,         // Pin 52 — 27 MHz
    input  wire rst_n,       // Pin 4  — S1 button (active LOW)
    input  wire rx,          // Pin 40 — UART RX from ESP32 GPIO17

    // Junction A outputs (external LEDs via 220Ω)
    output wire jA_red,      // Pin 25
    output wire jA_yellow,   // Pin 26
    output wire jA_green,    // Pin 27
    output wire jA_white,    // Pin 28

    // Junction B outputs (external LEDs via 220Ω)
    output wire jB_red,      // Pin 29
    output wire jB_yellow,   // Pin 30
    output wire jB_green,    // Pin 31
    output wire jB_white,    // Pin 32

    // Onboard status LEDs
    output reg  led_valid,   // Pin 10 (LED1) — pulses on valid packet
    output reg  led_invalid, // Pin 11 (LED2) — pulses on spoof attempt

    // FPGA ACK → ESP32 Gateway GPIO16
    // HIGH (level signal) while FSM is in EMERGENCY state
    // Pin 33 — see smart_traffic.cst
    output wire fpga_ack     // Pin 33
);

    // ── Internal wires ──────────────────────────────────────────
    wire [7:0] uart_data;
    wire       uart_valid;
    wire       emergency_out;
    wire       valid_pkt;
    wire       invalid_attempt;
    wire       in_emergency;   // Level signal: HIGH while FSM in EMERGENCY state

    // ── Module 1: UART Receiver ─────────────────────────────────
    uart_rx u_uart (
        .clk        (clk),
        .rst_n      (rst_n),
        .rx         (rx),
        .data       (uart_data),
        .data_valid (uart_valid)
    );

    // ── Module 2: Packet Validator ──────────────────────────────
    packet_validator u_validator (
        .clk            (clk),
        .rst_n          (rst_n),
        .data_valid     (uart_valid),
        .rx_data        (uart_data),
        .emergency_out  (emergency_out),
        .valid_packet   (valid_pkt),
        .invalid_attempt(invalid_attempt)
    );

    // ── Module 3: Traffic FSM ───────────────────────────────────
    traffic_fsm u_fsm (
        .clk         (clk),
        .rst_n       (rst_n),
        .emergency   (emergency_out),
        .valid_pkt   (valid_pkt),
        .jA_red      (jA_red),
        .jA_yellow   (jA_yellow),
        .jA_green    (jA_green),
        .jA_white    (jA_white),
        .jB_red      (jB_red),
        .jB_yellow   (jB_yellow),
        .jB_green    (jB_green),
        .jB_white    (jB_white),
        .in_emergency(in_emergency)
    );

    // fpga_ack: HIGH while in EMERGENCY → drives Pin 33 → ESP32 GPIO16
    assign fpga_ack = in_emergency;

    // ── Onboard LED blink on valid packet ───────────────────────
    // Stretch 1-cycle pulse to ~0.1 sec so eye can see it
    // 27 MHz × 0.1 sec = 2,700,000 cycles
    parameter BLINK_TIME = 32'd2_700_000;

    reg [31:0] blink_valid_cnt;
    reg [31:0] blink_inval_cnt;

    always @(posedge clk) begin
        if (!rst_n) begin
            led_valid       <= 1'b1;  // Onboard LEDs active LOW → 1 = OFF
            led_invalid     <= 1'b1;
            blink_valid_cnt <= 32'd0;
            blink_inval_cnt <= 32'd0;
        end else begin

            // Valid packet LED (LED1, Pin 10)
            if (valid_pkt) begin
                led_valid       <= 1'b0;  // Turn ON (active LOW)
                blink_valid_cnt <= 32'd0;
            end else if (blink_valid_cnt < BLINK_TIME) begin
                blink_valid_cnt <= blink_valid_cnt + 1;
            end else begin
                led_valid <= 1'b1;  // Turn OFF
            end

            // Invalid packet LED (LED2, Pin 11)
            if (invalid_attempt) begin
                led_invalid     <= 1'b0;  // Turn ON
                blink_inval_cnt <= 32'd0;
            end else if (blink_inval_cnt < BLINK_TIME) begin
                blink_inval_cnt <= blink_inval_cnt + 1;
            end else begin
                led_invalid <= 1'b1;  // Turn OFF
            end

        end
    end

endmodule