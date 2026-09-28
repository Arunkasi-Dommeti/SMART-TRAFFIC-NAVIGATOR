

module traffic_top (
    input  wire clk,         // Pin 52 — 27 MHz
    input  wire rst_n,       // Pin 4  — S1 button (active LOW)
    input  wire rx,          // Pin 40 — UART RX from ESP32 GPIO17

    // Junction A outputs 
    output wire jA_red,      // Pin 25
    output wire jA_yellow,   // Pin 26
    output wire jA_green,    // Pin 27
    output wire jA_white,    // Pin 28

    // Junction B outputs 
    output wire jB_red,      // Pin 29
    output wire jB_yellow,   // Pin 30
    output wire jB_green,    // Pin 31
    output wire jB_white,    // Pin 32

    // Onboard status LEDs
    output reg  led_valid,   // Pin 10 
    output reg  led_invalid, // Pin 11 

   
    output wire fpga_ack     // Pin 33
);

 
    wire [7:0]  uart_data;
    wire        uart_valid;
    wire        emergency_out;
    wire        valid_pkt;
    wire        invalid_attempt;
    wire        in_emergency;    
    wire [15:0] ambulance_id;   
    wire [1:0]  amb_priority;   

    uart_rx u_uart (
        .clk        (clk),
        .rst_n      (rst_n),
        .rx         (rx),
        .data       (uart_data),
        .data_valid (uart_valid)
    );


    packet_validator u_validator (
        .clk            (clk),
        .rst_n          (rst_n),
        .data_valid     (uart_valid),
        .rx_data        (uart_data),
        .emergency_out  (emergency_out),
        .valid_packet   (valid_pkt),
        .invalid_attempt(invalid_attempt),
        // Phase 4 scaffold outputs
        .ambulance_id   (ambulance_id),
        .amb_priority   (amb_priority)
    );


    traffic_fsm u_fsm (
        .clk         (clk),
        .rst_n       (rst_n),
        .emergency   (emergency_out),
        .valid_pkt   (valid_pkt),
        .amb_priority(amb_priority), 
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

    assign fpga_ack = in_emergency;


    parameter BLINK_TIME = 32'd2_700_000;

    reg [31:0] blink_valid_cnt;
    reg [31:0] blink_inval_cnt;

    always @(posedge clk) begin
        if (!rst_n) begin
            led_valid       <= 1'b1;  
            led_invalid     <= 1'b1;
            blink_valid_cnt <= 32'd0;
            blink_inval_cnt <= 32'd0;
        end else begin

            if (valid_pkt) begin
                led_valid       <= 1'b0;  
                blink_valid_cnt <= 32'd0;
            end else if (blink_valid_cnt < BLINK_TIME) begin
                blink_valid_cnt <= blink_valid_cnt + 1;
            end else begin
                led_valid <= 1'b1;  // Turn OFF
            end

        
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
