-------------------------------------------------------------------------
-- Matthew Dwyer
-- Department of Electrical and Computer Engineering
-- Iowa State University
-------------------------------------------------------------------------


-- piped_mac.vhd
-------------------------------------------------------------------------
-- DESCRIPTION: This file contains a basic piplined axi-stream mac unit. It
-- multiplies two integer values togeather and accumulates them.
--
-- NOTES:
-- 10/25/21 by MPD::Inital template creation
-- 9/5/25 by CWS::Minor changes to remove Qx.x
-------------------------------------------------------------------------

library work;
library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;

entity piped_mac is
  generic(
      -- Parameters of mac
      C_DATA_WIDTH : integer := 8;
      FourBytes    : integer := 32
    );
	port (
        ACLK	: in	std_logic;
		ARESETN	: in	std_logic;       

        -- AXIS slave data interface
		SD_AXIS_TREADY	: out	std_logic;
		SD_AXIS_TDATA	: in	std_logic_vector(C_DATA_WIDTH*2-1 downto 0);  -- Packed data input
		SD_AXIS_TLAST	: in	std_logic;
        SD_AXIS_TUSER   : in    std_logic;  -- Should we treat this first value in the stream as an inital accumulate value?
		SD_AXIS_TVALID	: in	std_logic;
        SD_AXIS_TID     : in    std_logic_vector(7 downto 0);

        -- AXIS master accumulate result out interface
		MO_AXIS_TVALID	: out	std_logic;
		MO_AXIS_TDATA	: out	std_logic_vector(31 downto 0);
		MO_AXIS_TLAST	: out	std_logic;
		MO_AXIS_TREADY	: in	std_logic;
		MO_AXIS_TID     : out   std_logic_vector(7 downto 0)
    );

attribute SIGIS : string; 
attribute SIGIS of ACLK : signal is "Clk"; 

end piped_mac;


architecture behavioral of piped_mac is
    --Mac stages
    type PIPE_STAGES is (INPUT_STAGE, MUL_STAGE, ADD_STAGE, WB_STAGE);

    -- Stage 0 -> 1 (INPUT_STAGE OUTPUT, MUL_STAGE INPUT)
    signal s0_valid : std_logic;
    signal s0_A     : signed(C_DATA_WIDTH-1 downto 0);
    signal s0_B     : signed(C_DATA_WIDTH-1 downto 0);
    signal s0_bias  : signed(FourBytes-1 downto 0);   -- FOR TUSER=1
    signal s0_user  : std_logic;
    signal s0_last  : std_logic;
    signal s0_tid   : std_logic_vector(7 downto 0);

    -- Stage 1 -> 2 (MUL_STAGE OUTPUT, ADD_STAGE INPUT)
    signal s1_valid   : std_logic;
    signal s1_product : signed(FourBytes-1 downto 0);

    attribute use_dsp : string;
    attribute use_dsp of s1_product : signal is "yes";
    
    signal s1_bias    : signed(FourBytes-1 downto 0);
    signal s1_user    : std_logic;
    signal s1_last    : std_logic;
    signal s1_tid     : std_logic_vector(7 downto 0);

    -- Stage 2 -> 3 (ADD_STAGE OUTPUT, WB_STAGE INPUT)
    signal s2_valid : std_logic;
    signal s2_sum   : signed(FourBytes-1 downto 0);
    signal s2_last  : std_logic;
    signal s2_tid   : std_logic_vector(7 downto 0);

    -- Running accumulator that persists across the whole group
    signal acc : signed(FourBytes-1 downto 0);

    -- Stalls the entire pipeline when the output cannot be sent
    signal stall : std_logic;

    -- Internal mirror of MO_AXIS_TVALID (out ports can't be read directly)
    signal mo_valid : std_logic;

    -- Debug signals
    signal mac_debug : std_logic_vector(31 downto 0);

begin

    -- Interface signals
    stall          <= '1' when (mo_valid = '1' and MO_AXIS_TREADY = '0') else '0';
    SD_AXIS_TREADY <= not stall;
    MO_AXIS_TVALID <= mo_valid;
    -- Internal signals
	
	
	-- Debug Signals
    mac_debug <= x"00000000";  -- Double checking sanity
   
   process (ACLK) is
    begin
        if rising_edge(ACLK) then

            if ARESETN = '0' then  -- Reset
                s0_valid <= '0';
                s1_valid <= '0';
                s2_valid <= '0';
                acc      <= (others => '0');

                mo_valid <= '0';
                MO_AXIS_TLAST  <= '0';
                MO_AXIS_TDATA  <= (others => '0');
                MO_AXIS_TID    <= (others => '0');

            elsif stall = '0' then  -- WHEN NO STALL GOING STAGE
                for i in PIPE_STAGES'left to PIPE_STAGES'right loop
                    case i is

                        when INPUT_STAGE =>
                            if SD_AXIS_TVALID = '1' then
                                s0_valid <= '1';
                                s0_A     <= signed(SD_AXIS_TDATA(C_DATA_WIDTH*2-1 downto C_DATA_WIDTH));
                                s0_B     <= signed(SD_AXIS_TDATA(C_DATA_WIDTH-1 downto 0));
                                s0_bias  <= resize(signed(SD_AXIS_TDATA), FourBytes);
                                s0_user  <= SD_AXIS_TUSER;
                                s0_last  <= SD_AXIS_TLAST;
                                s0_tid   <= SD_AXIS_TID;
                            else
                                s0_valid <= '0';
                            end if;

                        when MUL_STAGE =>
                            s1_valid   <= s0_valid;
                            s1_product <= resize(s0_A * s0_B, FourBytes);
                            s1_bias    <= s0_bias;
                            s1_user    <= s0_user;
                            s1_last    <= s0_last;
                            s1_tid     <= s0_tid;

                        when ADD_STAGE =>
                            s2_valid <= s1_valid;
                            s2_last  <= s1_last;
                            s2_tid   <= s1_tid;

                            if s1_valid = '1' then
                                if s1_user = '1' then
                                    s2_sum <= s1_bias;
                                    if s1_last = '1' then
                                        acc <= (others => '0');
                                    else
                                        acc <= s1_bias;
                                    end if;
                                else
                                    s2_sum <= acc + s1_product;
                                    if s1_last = '1' then
                                        acc <= (others => '0');
                                    else
                                        acc <= acc + s1_product;
                                    end if;
                                end if;
                            end if;

                        when WB_STAGE =>
                            if s2_valid = '1' and s2_last = '1' then
                                MO_AXIS_TDATA  <= std_logic_vector(s2_sum);
                                MO_AXIS_TID    <= s2_tid;
                                MO_AXIS_TLAST  <= '1';
                                mo_valid <= '1';
                            else
                                mo_valid <= '0';
                                
                            end if;

                    end case;
                end loop;
            end if;  -- stall

        end if;  -- Rising Edge
    end process;
end architecture behavioral;
