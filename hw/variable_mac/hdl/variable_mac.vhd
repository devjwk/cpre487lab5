-------------------------------------------------------------------------
-- CprE 487/587 Lab 5, Team 06
-------------------------------------------------------------------------
-- variable_mac.vhd
-------------------------------------------------------------------------
-- DESCRIPTION: Variable-precision AXI-Stream MAC unit using spatial
-- accumulation (EPDNN 7.4). One 32-bit word carries 16 bits of weights
-- (upper half) and 16 bits of activations (lower half). The operand width
-- is chosen per packet and decides how the same multiplier hardware is
-- used:
--     8 bit : 2 multiply-accumulates per word
--     4 bit : 4 multiply-accumulates per word
--     2 bit : 8 multiply-accumulates per word
-- Field i of the weights is multiplied by field i of the activations
-- (field 0 = least significant bits) and all products of a word are
-- added to the accumulator in the same cycle.
--
-- The multiplier is built from 2-bit x 2-bit bricks. Each half of the
-- word has 16 bricks: at 8 bit they are shifted and added into one 8x8
-- product, at 4 bit into two 4x4 products, and at 2 bit four of them are
-- used as they are.
--
-- PROTOCOL: the first word of every packet is a header. Bits [1:0] set
-- the operand width for that packet: 0 = 8 bit, 1 = 4 bit, 2 = 2 bit.
-- TLAST on the last word makes the unit send the 32-bit sum and clear
-- the accumulator.
-------------------------------------------------------------------------

library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;

entity variable_mac is
    port (
        ACLK    : in  std_logic;
        ARESETN : in  std_logic;

        -- AXIS slave data interface
        SD_AXIS_TREADY : out std_logic;
        SD_AXIS_TDATA  : in  std_logic_vector(31 downto 0);
        SD_AXIS_TLAST  : in  std_logic;
        SD_AXIS_TVALID : in  std_logic;
        SD_AXIS_TID    : in  std_logic_vector(7 downto 0);

        -- AXIS master accumulate result out interface
        MO_AXIS_TVALID : out std_logic;
        MO_AXIS_TDATA  : out std_logic_vector(31 downto 0);
        MO_AXIS_TLAST  : out std_logic;
        MO_AXIS_TREADY : in  std_logic;
        MO_AXIS_TID    : out std_logic_vector(7 downto 0)
    );

attribute SIGIS : string;
attribute SIGIS of ACLK : signal is "Clk";

end variable_mac;


architecture behavioral of variable_mac is

    -- Sum of the products of one 8-bit weight half and one 8-bit activation half.
    -- mode "00": one 8x8 product, "01": two 4x4 products, others: four 2x2 products.
    function fuse(w, a : std_logic_vector(7 downto 0); mode : std_logic_vector(1 downto 0)) return signed is
        variable ws, as2 : signed(2 downto 0);   -- one 2-bit segment, extended to 3 bits
        variable sum     : signed(15 downto 0) := (others => '0');
        variable used, top_i, top_j : boolean;
        variable sh      : natural;
    begin
        for i in 0 to 3 loop
            for j in 0 to 3 loop
                -- Which bricks belong to the same operand pair, which segment holds the sign bit of its
                -- operand, and where the brick's product sits in the result
                case mode is
                    when "00" =>
                        used := true;        top_i := (i = 3);         top_j := (j = 3);         sh := 2 * (i + j);
                    when "01" =>
                        used := (i / 2 = j / 2); top_i := (i mod 2 = 1); top_j := (j mod 2 = 1); sh := 2 * ((i mod 2) + (j mod 2));
                    when others =>
                        used := (i = j);     top_i := true;            top_j := true;            sh := 0;
                end case;

                -- The top segment of an operand is signed, the lower ones are unsigned
                ws  := '0' & signed(w(2 * i + 1 downto 2 * i));
                as2 := '0' & signed(a(2 * j + 1 downto 2 * j));
                if top_i then ws(2)  := w(2 * i + 1); end if;
                if top_j then as2(2) := a(2 * j + 1); end if;

                if used then
                    sum := sum + shift_left(resize(ws * as2, 16), sh);  -- 2x2 brick
                end if;
            end loop;
        end loop;
        return sum;
    end function;

    -- Stage 0 -> 1 (registered input word)
    signal s0_valid  : std_logic;
    signal s0_data   : std_logic_vector(31 downto 0);
    signal s0_last   : std_logic;
    signal s0_header : std_logic;
    signal s0_tid    : std_logic_vector(7 downto 0);

    -- Stage 1 -> 2 (sum of the products of one word)
    signal s1_valid : std_logic;
    signal s1_sum   : signed(17 downto 0);
    signal s1_last  : std_logic;
    signal s1_tid   : std_logic_vector(7 downto 0);

    signal expect_header : std_logic;                     -- the next word starts a packet
    signal mode          : std_logic_vector(1 downto 0);  -- operand width of the current packet
    signal acc           : signed(31 downto 0);           -- running sum of the current packet
    signal mo_valid      : std_logic;
    signal stall         : std_logic;                     -- the result is waiting to be taken

begin

    stall          <= mo_valid and not MO_AXIS_TREADY;
    SD_AXIS_TREADY <= not stall;
    MO_AXIS_TVALID <= mo_valid;
    MO_AXIS_TLAST  <= '1';  -- one result word per packet

    process (ACLK) is
        variable total : signed(31 downto 0);
    begin
        if rising_edge(ACLK) then
            if ARESETN = '0' then
                s0_valid      <= '0';
                s1_valid      <= '0';
                expect_header <= '1';
                mode          <= "00";
                acc           <= (others => '0');
                mo_valid      <= '0';
                MO_AXIS_TDATA <= (others => '0');
                MO_AXIS_TID   <= (others => '0');

            elsif stall = '0' then
                -- Stage 0: take a word
                s0_valid <= SD_AXIS_TVALID;
                if SD_AXIS_TVALID = '1' then
                    s0_data       <= SD_AXIS_TDATA;
                    s0_last       <= SD_AXIS_TLAST;
                    s0_tid        <= SD_AXIS_TID;
                    s0_header     <= expect_header;
                    expect_header <= SD_AXIS_TLAST;
                end if;

                -- Stage 1: the header sets the width, a data word is multiplied
                s1_valid <= s0_valid;
                s1_last  <= s0_last;
                s1_tid   <= s0_tid;
                if s0_valid = '1' then
                    if s0_header = '1' then
                        mode   <= s0_data(1 downto 0);
                        s1_sum <= (others => '0');
                    else
                        s1_sum <= resize(fuse(s0_data(31 downto 24), s0_data(15 downto 8), mode), 18)
                                + resize(fuse(s0_data(23 downto 16), s0_data(7 downto 0), mode), 18);
                    end if;
                end if;

                -- Stage 2: accumulate, and send the sum after the last word
                mo_valid <= '0';
                if s1_valid = '1' then
                    total := acc + resize(s1_sum, 32);
                    if s1_last = '1' then
                        MO_AXIS_TDATA <= std_logic_vector(total);
                        MO_AXIS_TID   <= s1_tid;
                        mo_valid      <= '1';
                        acc           <= (others => '0');
                    else
                        acc <= total;
                    end if;
                end if;
            end if;
        end if;
    end process;

end architecture behavioral;
