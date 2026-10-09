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
-- (field 0 = least significant bits). Partial sums pass through a
-- registered adder tree before they reach the accumulator.
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
    -- One mode-selected 2x2 brick, with no addition on this pipeline stage.
    function brick(w, a : std_logic_vector(7 downto 0);
                   mode : std_logic_vector(1 downto 0);
                   i, j : natural) return signed is
        variable ws, as2 : signed(2 downto 0);
        variable used, top_i, top_j : boolean;
        variable sh : natural;
    begin
        case mode is
            when "00" =>
                used := true; top_i := (i = 3); top_j := (j = 3); sh := 2 * (i + j);
            when "01" =>
                used := (i / 2 = j / 2); top_i := (i mod 2 = 1); top_j := (j mod 2 = 1);
                sh := 2 * ((i mod 2) + (j mod 2));
            when others =>
                used := (i = j); top_i := true; top_j := true; sh := 0;
        end case;
        ws := '0' & signed(w(2 * i + 1 downto 2 * i));
        as2 := '0' & signed(a(2 * j + 1 downto 2 * j));
        if top_i then ws(2) := w(2 * i + 1); end if;
        if top_j then as2(2) := a(2 * j + 1); end if;
        if used then
            return shift_left(resize(ws * as2, 16), sh);
        end if;
        return to_signed(0, 16);
    end function;

    type term_array is array (0 to 31) of signed(15 downto 0);
    type brick_pair_array is array (0 to 15) of signed(16 downto 0);
    type row_array is array (0 to 7) of signed(17 downto 0);
    type row_pair_array is array (0 to 3) of signed(18 downto 0);
    type group_array is array (0 to 1) of signed(19 downto 0);
    type tid_array is array (1 to 6) of std_logic_vector(7 downto 0);

    signal s0_valid  : std_logic;
    signal s0_data   : std_logic_vector(31 downto 0);
    signal s0_last   : std_logic;
    signal s0_header : std_logic;
    signal s0_tid    : std_logic_vector(7 downto 0);

    signal s1_terms : term_array;
    signal s2_pairs : brick_pair_array;
    signal s3_rows  : row_array;
    signal s4_pairs : row_pair_array;
    signal s5_groups : group_array;
    signal s6_sum   : signed(20 downto 0);
    signal valid_pipe, last_pipe : std_logic_vector(6 downto 1);
    signal tid_pipe : tid_array;

    signal expect_header : std_logic;
    signal mode          : std_logic_vector(1 downto 0);
    signal acc           : signed(31 downto 0);
    signal mo_valid      : std_logic;
    signal stall         : std_logic;
begin
    stall          <= mo_valid and not MO_AXIS_TREADY;
    SD_AXIS_TREADY <= not stall;
    MO_AXIS_TVALID <= mo_valid;
    MO_AXIS_TLAST  <= '1';

    process (ACLK) is
        variable total : signed(31 downto 0);
    begin
        if rising_edge(ACLK) then
            if ARESETN = '0' then
                s0_valid      <= '0';
                valid_pipe    <= (others => '0');
                expect_header <= '1';
                mode          <= "00";
                acc           <= (others => '0');
                mo_valid      <= '0';
                MO_AXIS_TDATA <= (others => '0');
                MO_AXIS_TID   <= (others => '0');
            elsif stall = '0' then
                -- Stage 0: accept the packet header or a data word.
                s0_valid <= SD_AXIS_TVALID;
                if SD_AXIS_TVALID = '1' then
                    s0_data       <= SD_AXIS_TDATA;
                    s0_last       <= SD_AXIS_TLAST;
                    s0_tid        <= SD_AXIS_TID;
                    s0_header     <= expect_header;
                    expect_header <= SD_AXIS_TLAST;
                end if;

                -- Stage 1: select the operand width and register individual bricks.
                valid_pipe(1) <= s0_valid;
                last_pipe(1) <= s0_last;
                tid_pipe(1) <= s0_tid;
                if s0_valid = '1' then
                    if s0_header = '1' then
                        mode <= s0_data(1 downto 0);
                        s1_terms <= (others => (others => '0'));
                    else
                        for i in 0 to 3 loop
                            for j in 0 to 3 loop
                                s1_terms(i * 4 + j) <= brick(s0_data(31 downto 24), s0_data(15 downto 8), mode, i, j);
                                s1_terms(16 + i * 4 + j) <= brick(s0_data(23 downto 16), s0_data(7 downto 0), mode, i, j);
                            end loop;
                        end loop;
                    end if;
                end if;

                -- Stages 2-6: one registered adder-tree level per stage.
                valid_pipe(2) <= valid_pipe(1);
                last_pipe(2) <= last_pipe(1);
                tid_pipe(2) <= tid_pipe(1);
                for i in 0 to 15 loop
                    s2_pairs(i) <= resize(s1_terms(2 * i), 17) + resize(s1_terms(2 * i + 1), 17);
                end loop;

                valid_pipe(3) <= valid_pipe(2);
                last_pipe(3) <= last_pipe(2);
                tid_pipe(3) <= tid_pipe(2);
                for i in 0 to 7 loop
                    s3_rows(i) <= resize(s2_pairs(2 * i), 18) + resize(s2_pairs(2 * i + 1), 18);
                end loop;

                valid_pipe(4) <= valid_pipe(3);
                last_pipe(4) <= last_pipe(3);
                tid_pipe(4) <= tid_pipe(3);
                for i in 0 to 3 loop
                    s4_pairs(i) <= resize(s3_rows(2 * i), 19) + resize(s3_rows(2 * i + 1), 19);
                end loop;

                valid_pipe(5) <= valid_pipe(4);
                last_pipe(5) <= last_pipe(4);
                tid_pipe(5) <= tid_pipe(4);
                for i in 0 to 1 loop
                    s5_groups(i) <= resize(s4_pairs(2 * i), 20) + resize(s4_pairs(2 * i + 1), 20);
                end loop;

                valid_pipe(6) <= valid_pipe(5);
                last_pipe(6) <= last_pipe(5);
                tid_pipe(6) <= tid_pipe(5);
                s6_sum <= resize(s5_groups(0), 21) + resize(s5_groups(1), 21);

                -- Stage 7: accumulate and return one result per packet.
                mo_valid <= '0';
                if valid_pipe(6) = '1' then
                    total := acc + resize(s6_sum, 32);
                    if last_pipe(6) = '1' then
                        MO_AXIS_TDATA <= std_logic_vector(total);
                        MO_AXIS_TID   <= tid_pipe(6);
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
