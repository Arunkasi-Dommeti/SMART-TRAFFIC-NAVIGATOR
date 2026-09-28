

module traffic_fsm (
    input  wire clk,
    input  wire rst_n,        
    input  wire emergency,    
    input  wire valid_pkt,    
    input  wire [1:0] amb_priority,    

    output reg  jA_red,
    output reg  jA_yellow,
    output reg  jA_green,
    output reg  jA_white,

    output reg  jB_red,
    output reg  jB_yellow,
    output reg  jB_green,
    output reg  jB_white,

    output reg  in_emergency
);


    parameter IDLE      = 3'd0;
    parameter S1_GREEN  = 3'd1;
    parameter S1_YELLOW = 3'd2;
    parameter S2_GREEN  = 3'd3;
    parameter S2_YELLOW = 3'd4;
    parameter EMERGENCY = 3'd5;

    reg [2:0] state;


`ifdef SIMULATION

    parameter GREEN_TIME  = 32'd200;
    parameter YELLOW_TIME = 32'd80;
    parameter EMERG_TIME  = 32'd400;
    parameter DETECT_TIME = 32'd40;
`else

    parameter GREEN_TIME  = 32'd405_000_000;  
    parameter YELLOW_TIME = 32'd54_000_000;
    parameter EMERG_TIME  = 32'd810_000_000;  
    parameter DETECT_TIME = 32'd13_500_000;
`endif

    reg [31:0] counter;


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

    always @(*) begin
       
        jA_red       = 1'b1;
        jA_yellow    = 1'b0;
        jA_green     = 1'b0;
        jA_white     = 1'b0;
        jB_red       = 1'b1;
        jB_yellow    = 1'b0;
        jB_green     = 1'b0;
        jB_white     = 1'b0;
        in_emergency = 1'b0;  

        case (state)

            IDLE: begin
            
                jA_red = 1'b1;
                jB_red = 1'b1;
            end

            S1_GREEN: begin
             
                jA_red    = 1'b0;
                jA_green  = 1'b1;
                jA_white  = 1'b0;
                jB_red    = 1'b1;
                jB_white  = 1'b1;   
            end

            S1_YELLOW: begin
                
               
                jA_red    = 1'b0;
                jA_yellow = 1'b1;
                jA_white  = 1'b0;
                jB_red    = 1'b1;
                jB_white  = 1'b0;   
            end

            S2_GREEN: begin

                jA_red    = 1'b1;
                jA_white  = 1'b1;   
                jB_red    = 1'b0;
                jB_green  = 1'b1;
                jB_white  = 1'b0;
            end

            S2_YELLOW: begin
            
                jA_red    = 1'b1;
                jA_white  = 1'b0;
                jB_red    = 1'b0;
                jB_yellow = 1'b1;
                jB_white  = 1'b0;
            end

            EMERGENCY: begin
             
                jA_red       = 1'b0;
                jA_green     = 1'b1;
                jA_white     = 1'b0;
                jB_red       = 1'b1;
                jB_yellow    = 1'b0;
                jB_green     = 1'b0;
                jB_white     = 1'b1;  
                in_emergency = 1'b1; 
            end

            default: begin
                jA_red = 1'b1;
                jB_red = 1'b1;
            end

        endcase
    end

endmodule
