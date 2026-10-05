-------------------------------------------------------------------------
-- Matthew Dwyer
-- Department of Electrical and Computer Engineering
-- Iowa State University
-------------------------------------------------------------------------


-- staged_mac.vhd
-------------------------------------------------------------------------
-- DESCRIPTION: This file contains a basic staged axi-stream mac unit. It
-- multiplies two integer values together and accumulates them.
--
-- NOTES:
-- 10/25/21 by MPD::Inital template creation
-- 9/5/25 by CWS::Minor changes to remove Qx.x
-------------------------------------------------------------------------

library work;
library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;

entity staged_mac is
  generic(
      -- Parameters of mac
      C_DATA_WIDTH : integer := 8
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

end staged_mac;


architecture behavioral of staged_mac is
    -- Internal Signals
	signal acc : signed(31 downto 0); --32-bit accumulator
    signal id_reg : std_logic_vector(7 downto 0); -- TID to send out with result
	signal out_valid : std_logic; -- flag: result is ready
	-- Mac state
    type STATE_TYPE is (WAIT_FOR_VALUES, SEND_RESULT);
    signal state : STATE_TYPE;
	
	-- Debug signals, make sure we aren't going crazy
    signal mac_debug : std_logic_vector(31 downto 0);

begin

    -- Interface signals
    SD_AXIS_TREADY <= '1' when state = WAIT_FOR_VALUES else '0';
    MO_AXIS_TVALID <= out_valid;
    MO_AXIS_TDATA  <= std_logic_vector(acc);
    MO_AXIS_TLAST  <= '1';   -- staged_mac only ever produces one result per group
    MO_AXIS_TID    <= id_reg;

    -- Internal signals
	
	
	-- Debug Signals
    mac_debug <= x"00000000";  -- Double checking sanity
   
   process (ACLK) is
   begin 
    if rising_edge(ACLK) then  -- Rising Edge

      -- Reset values if reset is low
      if ARESETN = '0' then  -- Reset
        state       <= WAIT_FOR_VALUES;
        acc       <= (others => '0');
        out_valid <= '0';
        id_reg    <= (others => '0');

      else
        case state is  -- State
            -- Wait here until we receive values
            when WAIT_FOR_VALUES =>
                -- Wait here until we recieve valid values
                -- Being in this state implies TREADY = '1'
                if SD_AXIS_TVALID = '1' then
                    id_reg <= SD_AXIS_TID;

                    if SD_AXIS_TUSER = '1' then
                        -- Load bias (initial value): sign-extend 16-bit to 32-bit
                        acc <= resize(signed(SD_AXIS_TDATA), 32);
                    else
                        -- Multiply-accumulate
                        acc <= acc + resize(
                                 signed(SD_AXIS_TDATA(C_DATA_WIDTH*2-1 downto C_DATA_WIDTH)) *
                                 signed(SD_AXIS_TDATA(C_DATA_WIDTH-1 downto 0)),
                                 32);
                    end if;

                    if SD_AXIS_TLAST = '1' then
                        state     <= SEND_RESULT;
                        out_valid <= '1';
                    end if;
                end if;

            when SEND_RESULT =>
                if MO_AXIS_TREADY = '1' then
                    out_valid <= '0';
                    acc       <= (others => '0'); -- clear accumulator for the next independent group
                    state     <= WAIT_FOR_VALUES;
                end if;
			
            when others =>
                state <= WAIT_FOR_VALUES;
                -- Not really important, this case should never happen
                -- Needed for proper synthisis         
        end case;  -- State
      end if;  -- Reset

    end if;  -- Rising Edge
   end process;
end architecture behavioral;
