// ============================================================
// Module  : traffic_fsm.v
// Board   : Tang Nano 9K (27 MHz)
// Junctions: 2 (Junction A = ambulance lane, Junction B = cross)
//
// FSM States:
//   IDLE      → Initialize, go to S1_GREEN
//   S1_GREEN  → Jct A GREEN, Jct B RED  (15 seconds)
//   S1_YELLOW → Jct A YELLOW, Jct B RED (2 seconds)
//   S2_GREEN  → Jct A RED, Jct B GREEN  (15 seconds)
//   S2_YELLOW → Jct A RED, Jct B YELLOW (2 seconds)
//   EMERGENCY → Jct A GREEN, Jct B RED  (30 seconds)
//              Preempts any state immediately on valid packet
//
// Timing at 27 MHz:
//   1 sec = 27,000,000 cycles
//   GREEN    = 15 sec = 405,000,000 cycles
//   YELLOW   =  2 sec =  54,000,000 cycles
//   EMERGENCY= 30 sec = 810,000,000 cycles
//   DETECT   =  0.5s  =  13,500,000 cycles
//
// Output pins (external LEDs via 220Ω resistors):
//   jA_red/yellow/green/white → Junction A (ambulance lane)
//   jB_red/yellow/green/white → Junction B (cross traffic)
//
// LED logic: HIGH = LED ON, LOW = LED OFF
// ============================================================

module traffic_fsm (
    input  wire clk,
    input  wire rst_n,        // Active LOW reset
    input  wire emergency,    // From packet_validator emergency_out
    input  wire valid_pkt,    // From packet_validator valid_packet (pulse)

    output reg  jA_red,
    output reg  jA_yellow,
    output reg  jA_green,
    output reg  jA_white,     // Pedestrian / indicator

    output reg  jB_red,
    output reg  jB_yellow,
    output reg  jB_green,
    output reg  jB_white
);

    // ── State encoding ──────────────────────────────────────────
    parameter IDLE      = 3'd0;
    parameter S1_GREEN  = 3'd1;
    parameter S1_YELLOW = 3'd2;
    parameter S2_GREEN  = 3'd3;
    parameter S2_YELLOW = 3'd4;
    parameter EMERGENCY = 3'd5;

    reg [2:0] state;

    // ── Timing constants (27 MHz) ───────────────────────────────
    // For SIMULATION: uncomment small values, comment real values
    // parameter GREEN_TIME  = 32'd200;
    // parameter YELLOW_TIME = 32'd80;
    // parameter EMERG_TIME  = 32'd400;
    // parameter DETECT_TIME = 32'd40;

    // Real hardware values (27 MHz):
    parameter GREEN_TIME  = 32'd405_000_000;  // 15 seconds
    parameter YELLOW_TIME = 32'd54_000_000;   //  2 seconds
    parameter EMERG_TIME  = 32'd810_000_000;  // 30 seconds
    parameter DETECT_TIME = 32'd13_500_000;   //  0.5 seconds

    reg [31:0] counter;

    // ── Sequential logic ───────────────────────────────────────
    always @(posedge clk) begin
        if (!rst_n) begin
            state   <= IDLE;
            counter <= 32'd0;
        end else begin
            counter <= counter + 1;

            case (state)

                IDLE: begin
                    counter <= 32'd0;
                    state   <= S1_GREEN;
                end

                S1_GREEN: begin
                    // Emergency preempts immediately
                    if (valid_pkt && emergency) begin
                        state   <= EMERGENCY;
                        counter <= 32'd0;
                    end else if (counter >= GREEN_TIME) begin
                        state   <= S1_YELLOW;
                        counter <= 32'd0;
                    end
                end

                S1_YELLOW: begin
                    if (valid_pkt && emergency) begin
                        state   <= EMERGENCY;
                        counter <= 32'd0;
                    end else if (counter >= YELLOW_TIME) begin
                        state   <= S2_GREEN;
                        counter <= 32'd0;
                    end
                end

                S2_GREEN: begin
                    if (valid_pkt && emergency) begin
                        state   <= EMERGENCY;
                        counter <= 32'd0;
                    end else if (counter >= GREEN_TIME) begin
                        state   <= S2_YELLOW;
                        counter <= 32'd0;
                    end
                end

                S2_YELLOW: begin
                    if (valid_pkt && emergency) begin
                        state   <= EMERGENCY;
                        counter <= 32'd0;
                    end else if (counter >= YELLOW_TIME) begin
                        state   <= S1_GREEN;
                        counter <= 32'd0;
                    end
                end

                EMERGENCY: begin
                    // Hold for 30 seconds then return to S1_GREEN
                    // Also: if cancel packet arrives early, reset
                    if (valid_pkt && !emergency) begin
                        state   <= S1_GREEN;
                        counter <= 32'd0;
                    end else if (counter >= EMERG_TIME) begin
                        state   <= S1_GREEN;
                        counter <= 32'd0;
                    end
                end

                default: begin
                    state   <= IDLE;
                    counter <= 32'd0;
                end

            endcase
        end
    end

    // ── Combinational output logic ─────────────────────────────
    always @(*) begin
        // Safe defaults: all RED
        jA_red    = 1'b1;
        jA_yellow = 1'b0;
        jA_green  = 1'b0;
        jA_white  = 1'b0;
        jB_red    = 1'b1;
        jB_yellow = 1'b0;
        jB_green  = 1'b0;
        jB_white  = 1'b0;

        case (state)

            IDLE: begin
                // All RED during startup
                jA_red = 1'b1; jB_red = 1'b1;
            end

            S1_GREEN: begin
                // Junction A: GREEN + pedestrian stop
                // Junction B: RED   + pedestrian walk
                jA_red    = 1'b0;
                jA_green  = 1'b1;
                jA_white  = 1'b0;   // No pedestrian on active lane
                jB_red    = 1'b1;
                jB_white  = 1'b1;   // Pedestrian can walk on stopped lane
            end

            S1_YELLOW: begin
                // Junction A transitioning: YELLOW
                // Junction B: RED
                jA_red    = 1'b0;
                jA_yellow = 1'b1;
                jA_white  = 1'b0;
                jB_red    = 1'b1;
                jB_white  = 1'b0;   // Pedestrian stop during transition
            end

            S2_GREEN: begin
                // Junction A: RED   + pedestrian walk
                // Junction B: GREEN + pedestrian stop
                jA_red    = 1'b1;
                jA_white  = 1'b1;   // Pedestrian can walk
                jB_red    = 1'b0;
                jB_green  = 1'b1;
                jB_white  = 1'b0;
            end

            S2_YELLOW: begin
                // Junction B transitioning: YELLOW
                jA_red    = 1'b1;
                jA_white  = 1'b0;
                jB_red    = 1'b0;
                jB_yellow = 1'b1;
                jB_white  = 1'b0;
            end

            EMERGENCY: begin
                // Ambulance corridor:
                // Junction A: GREEN (ambulance passes)
                // Junction B: RED   (all stopped)
                // White on B: pedestrian warning
                jA_red    = 1'b0;
                jA_green  = 1'b1;
                jA_white  = 1'b0;
                jB_red    = 1'b1;
                jB_yellow = 1'b0;
                jB_green  = 1'b0;
                jB_white  = 1'b1;   // Warning: ambulance approaching
            end

            default: begin
                jA_red = 1'b1; jB_red = 1'b1;
            end

        endcase
    end

endmodule
