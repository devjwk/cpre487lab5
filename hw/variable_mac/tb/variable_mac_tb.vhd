-------------------------------------------------------------------------
-- variable_mac_tb.vhd
-------------------------------------------------------------------------
-- Replays the packets of vectors.txt (written by gen_vectors.cpp from the
-- board code of the framework) into variable_mac and checks every result.
-- Each line is "<number of words> <word 0> ... <word n-1> <expected>",
-- every field 8 hex digits. The packets are sent in three phases:
-- full speed, with random gaps in TVALID, and with random back-pressure
-- on the result as well.
-------------------------------------------------------------------------
library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;
use IEEE.math_real.all;
use std.textio.all;
use std.env.all;

entity variable_mac_tb is
    generic (VECTORS : string := "vectors.txt");
end entity;

architecture sim of variable_mac_tb is
    signal ACLK    : std_logic := '0';
    signal ARESETN : std_logic := '0';
    signal SD_AXIS_TREADY, SD_AXIS_TLAST, SD_AXIS_TVALID : std_logic := '0';
    signal SD_AXIS_TDATA  : std_logic_vector(31 downto 0) := (others => '0');
    signal SD_AXIS_TID    : std_logic_vector(7 downto 0)  := (others => '0');
    signal MO_AXIS_TVALID, MO_AXIS_TLAST : std_logic;
    signal MO_AXIS_TREADY : std_logic := '1';
    signal MO_AXIS_TDATA  : std_logic_vector(31 downto 0);
    signal MO_AXIS_TID    : std_logic_vector(7 downto 0);

    signal sent  : natural := 0;  -- packets fully sent
    signal slow  : natural := 0;  -- 0: full speed, 1: gaps in TVALID, 2: gaps and back-pressure
begin
    ACLK <= not ACLK after 5 ns;

    dut : entity work.variable_mac
        port map (ACLK => ACLK, ARESETN => ARESETN,
                  SD_AXIS_TREADY => SD_AXIS_TREADY, SD_AXIS_TDATA => SD_AXIS_TDATA, SD_AXIS_TLAST => SD_AXIS_TLAST,
                  SD_AXIS_TVALID => SD_AXIS_TVALID, SD_AXIS_TID => SD_AXIS_TID,
                  MO_AXIS_TVALID => MO_AXIS_TVALID, MO_AXIS_TDATA => MO_AXIS_TDATA, MO_AXIS_TLAST => MO_AXIS_TLAST,
                  MO_AXIS_TREADY => MO_AXIS_TREADY, MO_AXIS_TID => MO_AXIS_TID);

    -- Sends every packet of the file
    sender : process
        file f        : text;
        variable l    : line;
        variable n, w : std_logic_vector(31 downto 0);
        variable s1   : positive := 11;
        variable s2   : positive := 487;
        variable r    : real;
        variable total : natural := 0;
    begin
        -- count the packets to place the phase changes
        file_open(f, VECTORS, read_mode);
        while not endfile(f) loop
            readline(f, l);
            total := total + 1;
        end loop;
        file_close(f);

        wait for 42 ns;
        wait until rising_edge(ACLK);
        ARESETN <= '1';
        wait until rising_edge(ACLK);

        file_open(f, VECTORS, read_mode);
        while not endfile(f) loop
            readline(f, l);
            hread(l, n);
            if sent >= 2 * total / 3 then slow <= 2; elsif sent >= total / 3 then slow <= 1; end if;
            for i in 0 to to_integer(unsigned(n)) - 1 loop
                hread(l, w);
                if slow > 0 then  -- idle cycles before this word
                    uniform(s1, s2, r);
                    while r < 0.3 loop
                        SD_AXIS_TVALID <= '0';
                        wait until rising_edge(ACLK);
                        uniform(s1, s2, r);
                    end loop;
                end if;
                SD_AXIS_TVALID <= '1';
                SD_AXIS_TDATA  <= w;
                SD_AXIS_TID    <= std_logic_vector(to_unsigned(sent mod 256, 8));
                SD_AXIS_TLAST  <= '1' when i = to_integer(unsigned(n)) - 1 else '0';
                wait until rising_edge(ACLK) and SD_AXIS_TREADY = '1';
            end loop;
            sent <= sent + 1;
        end loop;
        SD_AXIS_TVALID <= '0';
        SD_AXIS_TLAST  <= '0';
        wait;
    end process;

    -- Random back-pressure on the result in the last phase
    ready : process
        variable s1 : positive := 5;
        variable s2 : positive := 2026;
        variable r  : real;
    begin
        wait until rising_edge(ACLK);
        uniform(s1, s2, r);
        MO_AXIS_TREADY <= '0' when (slow = 2 and r < 0.4) else '1';
    end process;

    -- Checks every result against the last field of its line
    checker : process
        file f            : text;
        variable l        : line;
        variable n, w     : std_logic_vector(31 downto 0);
        variable got      : natural := 0;
        variable errors   : natural := 0;
    begin
        file_open(f, VECTORS, read_mode);
        while not endfile(f) loop
            readline(f, l);
            hread(l, n);
            for i in 0 to to_integer(unsigned(n)) loop  -- skip the words, w ends as the expected result
                hread(l, w);
            end loop;
            wait until rising_edge(ACLK) and MO_AXIS_TVALID = '1' and MO_AXIS_TREADY = '1';
            if MO_AXIS_TDATA /= w or MO_AXIS_TLAST /= '1' or MO_AXIS_TID /= std_logic_vector(to_unsigned(got mod 256, 8)) then
                errors := errors + 1;
                if errors <= 10 then
                    report "packet " & integer'image(got) & ": got " & to_hstring(MO_AXIS_TDATA) & ", expected " & to_hstring(w) severity warning;
                end if;
            end if;
            got := got + 1;
        end loop;
        -- nothing more may come out
        for i in 1 to 20 loop
            wait until rising_edge(ACLK);
            if MO_AXIS_TVALID = '1' then errors := errors + 1; end if;
        end loop;
        report "variable_mac_tb: " & integer'image(got) & " packets checked, " & integer'image(errors) & " errors";
        assert errors = 0 report "TEST FAILED" severity failure;
        report "TEST PASSED";
        finish;
    end process;

    watchdog : process
    begin
        wait for 200 ms;
        report "TIMEOUT: the testbench did not finish" severity failure;
    end process;
end architecture;
