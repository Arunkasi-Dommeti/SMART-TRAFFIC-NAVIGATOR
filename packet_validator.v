

module packet_validator (
    input  wire       clk,
    input  wire       rst_n,
    input  wire       data_valid,    // From uart_rx
    input  wire [7:0] rx_data,       // From uart_rx

    output reg        emergency_out,
    output reg        valid_packet,
    output reg        invalid_attempt,

  
    output reg [15:0] ambulance_id,    
    output reg [1:0]  amb_priority     
);

    
    parameter SECRET_KEY = 8'h5A;

    
    parameter WAIT_CMD  = 2'd0;
    parameter WAIT_ID_H = 2'd1;
    parameter WAIT_ID_L = 2'd2;
    parameter WAIT_CHK  = 2'd3;

    reg [1:0] state;
    reg [7:0] r_cmd, r_id_h, r_id_l;

    wire [7:0] expected_chk;
    assign expected_chk = r_cmd ^ r_id_h ^ r_id_l ^ SECRET_KEY;

 

    function automatic is_whitelisted;
        input [7:0] h, l;
        begin
            is_whitelisted =
                (h == 8'h00 && l == 8'h01) ||  // AMB-001 
                (h == 8'h00 && l == 8'h02) ||  // AMB-002
                (h == 8'h00 && l == 8'h03);    // AMB-003
        end
    endfunction

    function [1:0] get_priority;
        input [7:0] h, l;
        begin
            if      (h == 8'h00 && l == 8'h01) get_priority = 2'd3; // AMB-001: CRITICAL
            else if (h == 8'h00 && l == 8'h02) get_priority = 2'd2; // AMB-002: HIGH
            else if (h == 8'h00 && l == 8'h03) get_priority = 2'd1; // AMB-003: MEDIUM
            else                                get_priority = 2'd0; // Unknown: LOW
        end
    endfunction

    always @(posedge clk) begin
        if (!rst_n) begin
            state           <= WAIT_CMD;
            emergency_out   <= 1'b0;
            valid_packet    <= 1'b0;
            invalid_attempt <= 1'b0;
            ambulance_id    <= 16'h0;   // Phase 4 scaffold
            amb_priority    <= 2'd0;    // Phase 4 scaffold
            r_cmd           <= 8'h0;
            r_id_h          <= 8'h0;
            r_id_l          <= 8'h0;
           
        end else begin
            
            valid_packet    <= 1'b0;
            invalid_attempt <= 1'b0;

            if (data_valid) begin
                case (state)

                    WAIT_CMD: begin
                   
                        if (rx_data == 8'h31 || rx_data == 8'h30) begin
                            r_cmd <= rx_data;
                            state <= WAIT_ID_H;
                        end
                      
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
                 

                        if (rx_data == expected_chk &&
                            is_whitelisted(r_id_h, r_id_l)) begin
                            // VALID PACKET
                            emergency_out <= (r_cmd == 8'h31) ? 1'b1 : 1'b0;
                            valid_packet  <= 1'b1;
                         
                            ambulance_id  <= {r_id_h, r_id_l};
                            amb_priority  <= get_priority(r_id_h, r_id_l);
                        end else begin
                            // INVALID — spoofed or unknown ID
                            invalid_attempt <= 1'b1;
                        end
                        state <= WAIT_CMD;  // Always reset to wait next packet
                    end

                endcase
            end
        end
    end

endmodule
