// ============================================================
// Module  : uart_rx.v
// Board   : Tang Nano 9K (Gowin GW1NR-9)
// Clock   : 27 MHz
// Baud    : 9600
// CLKS_PER_BIT = 27_000_000 / 9600 = 2813
//
// Function:
//   Receives serial bytes from ESP32 Gateway GPIO17
//   Uses double-sync on RX input to prevent metastability
//   Samples each bit at middle of bit period (HALF_BIT)
//   Pulses data_valid HIGH for exactly 1 clock cycle
//
// Connection:
//   rx pin → Tang Nano 9K physical Pin 40 (IOB33B, BANK2, 3.3V)
//   10kΩ pull-down resistor between Pin 40 and GND
// ============================================================

module uart_rx (
    input  wire       clk,        // 27 MHz
    input  wire       rst_n,      // Active LOW (S1 button = Pin 4)
    input  wire       rx,         // UART RX from ESP32 GPIO17 → Pin 40
    output reg  [7:0] data,       // Received byte
    output reg        data_valid  // HIGH for 1 clock cycle when byte ready
);

    // 27 MHz / 9600 baud = 2812.5 → round to 2813
    parameter CLKS_PER_BIT = 2813;
    parameter HALF_BIT     = CLKS_PER_BIT / 2;  // = 1406

    // Double-sync registers (prevent metastability from async RX)
    reg rx_s1, rx_s2;
    always @(posedge clk) begin
        rx_s1 <= rx;
        rx_s2 <= rx_s1;
    end

    reg [11:0] cnt;      // 12-bit: needs to count up to 2813
    reg [3:0]  bit_idx;  // bit index 0..8 (8 data + stop)
    reg [7:0]  shift;    // shift register for incoming bits
    reg        busy;     // currently receiving

    always @(posedge clk) begin
        if (!rst_n) begin
            data_valid <= 1'b0;
            busy       <= 1'b0;
            cnt        <= 12'd0;
            bit_idx    <= 4'd0;
            data       <= 8'd0;
            shift      <= 8'd0;
        end else begin
            data_valid <= 1'b0;  // Default: no valid pulse

            if (!busy && rx_s2 == 1'b0) begin
                // Start bit detected (RX went LOW)
                busy    <= 1'b1;
                cnt     <= 12'd0;
                bit_idx <= 4'd0;

            end else if (busy) begin
                cnt <= cnt + 1;

                // At half-period: verify start bit still LOW
                if (cnt == HALF_BIT && bit_idx == 4'd0) begin
                    if (rx_s2 != 1'b0) begin
                        busy <= 1'b0;  // False trigger — abort
                    end
                    cnt <= 12'd0;

                end else if (cnt >= CLKS_PER_BIT) begin
                    cnt <= 12'd0;

                    if (bit_idx < 4'd8) begin
                        // Sample data bit (LSB first)
                        shift[bit_idx] <= rx_s2;
                        bit_idx <= bit_idx + 1;

                    end else begin
                        // Stop bit — check it is HIGH
                        if (rx_s2 == 1'b1) begin
                            data       <= shift;
                            data_valid <= 1'b1;
                        end
                        busy <= 1'b0;
                    end
                end
            end
        end
    end

endmodule
