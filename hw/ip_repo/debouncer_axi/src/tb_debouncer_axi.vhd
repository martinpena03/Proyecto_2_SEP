-- =============================================================================
-- tb_debouncer_axi.vhd - Testbench autoverificado del IP debouncer_axi (AO3).
--
-- Sep Hero (IEE2463, Proyecto 2). Autores: grupo Sep Hero.
-- Fuente: diseño propio; los procedimientos de bus AXI siguen el estilo de
--   tb_buzzer_axi.vhd (AYU10) y tb_axi_seq.vhd (Proyecto 1).
--
-- Se simula con G_TICKS_PER_MS = 10: 1 "ms" = 10 ciclos de 20 ns = 200 ns.
-- Los botones se modelan activos en bajo (reposo = '1') y se usa CTRL.INV = 1.
--
-- Casos (docs/MANUAL_IMPLEMENTACION.md §5.6):
--   T1  reset y lectura de los 5 registros
--   T2  pulsación con rebotes -> una sola transición de STATE, EDGE e irq
--   T3  borrado W1C de EDGE -> irq baja
--   T4  BTN_MASK deshabilita el flanco pero no STATE
--   T5  CTRL.IRQ_EN = 0 -> EDGE se activa pero irq no
--   T6  CTRL.EN = 0 -> STATE = 0 y sin flancos
--   T7  flanco nuevo en el mismo ciclo que el W1C del mismo bit -> gana el flanco
--   T8  botón virtual (VIO) equivale a presionar
--   T9  glitch más corto que DEB_MS -> sin cambio
--   T10 CTRL.INV = 0 con los botones en reposo -> STATE = 111 (polaridad)
--
-- Resultado: imprime "TB_RESULT: PASS" o "TB_RESULT: FAIL" (lo busca 00_sim_ip.ps1).
-- =============================================================================
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_debouncer_axi is
end entity;

architecture sim of tb_debouncer_axi is
  constant TCLK : time     := 20 ns;       -- 50 MHz
  constant TPM  : positive := 10;          -- ciclos de reloj por "ms" en simulación

  -- Offsets de los registros.
  constant R_CTRL  : natural := 16#00#;
  constant R_DEBMS : natural := 16#04#;
  constant R_MASK  : natural := 16#08#;
  constant R_STATE : natural := 16#0C#;
  constant R_EDGE  : natural := 16#10#;

  signal clk      : std_logic := '0';
  signal rstn     : std_logic := '0';
  signal done     : boolean   := false;

  signal btn_in   : std_logic_vector(2 downto 0) := "111";   -- activos en bajo, en reposo
  signal btn_virt : std_logic_vector(2 downto 0) := "000";
  signal irq      : std_logic;
  signal dbg_state, dbg_edge : std_logic_vector(2 downto 0);
  signal dbg_tick : std_logic;

  signal awaddr, araddr : std_logic_vector(4 downto 0) := (others => '0');
  signal awvalid, wvalid, bready, arvalid, rready : std_logic := '0';
  signal awready, wready, bvalid, arready, rvalid : std_logic;
  signal wdata, rdata   : std_logic_vector(31 downto 0) := (others => '0');
  signal wstrb          : std_logic_vector(3 downto 0)  := "0000";
  signal bresp, rresp   : std_logic_vector(1 downto 0);

  -- Contadores del monitor: flancos de subida de STATE y de bajada de EDGE por bit.
  type nat_arr is array (0 to 2) of natural;
  signal st_rises : nat_arr := (others => 0);
  signal leds     : std_logic_vector(3 downto 0);
  signal ed_falls : nat_arr := (others => 0);
begin
  -- Reloj de 50 MHz; se detiene cuando el estímulo termina (así "run all" retorna).
  clkgen : process
  begin
    while not done loop
      clk <= '0';
      wait for TCLK/2;
      clk <= '1';
      wait for TCLK/2;
    end loop;
    clk <= '0';
    wait;
  end process;

  dut : entity work.debouncer_axi
    generic map (G_WIDTH => 3, G_TICKS_PER_MS => TPM)
    port map (
      btn_in => btn_in, btn_virt => btn_virt, irq => irq,
      dbg_state => dbg_state, dbg_edge => dbg_edge, dbg_tick => dbg_tick, leds => leds,
      s00_axi_aclk => clk, s00_axi_aresetn => rstn,
      s00_axi_awaddr => awaddr, s00_axi_awprot => "000",
      s00_axi_awvalid => awvalid, s00_axi_awready => awready,
      s00_axi_wdata => wdata, s00_axi_wstrb => wstrb,
      s00_axi_wvalid => wvalid, s00_axi_wready => wready,
      s00_axi_bresp => bresp, s00_axi_bvalid => bvalid, s00_axi_bready => bready,
      s00_axi_araddr => araddr, s00_axi_arprot => "000",
      s00_axi_arvalid => arvalid, s00_axi_arready => arready,
      s00_axi_rdata => rdata, s00_axi_rresp => rresp,
      s00_axi_rvalid => rvalid, s00_axi_rready => rready);

  -- Monitor: cuenta transiciones para poder afirmar "una sola" o "ninguna".
  mon : process(clk)
    variable prev_s, prev_e : std_logic_vector(2 downto 0) := "000";
  begin
    if rising_edge(clk) then
      for i in 0 to 2 loop
        if dbg_state(i) = '1' and prev_s(i) = '0' then
          st_rises(i) <= st_rises(i) + 1;
        end if;
        if dbg_edge(i) = '0' and prev_e(i) = '1' then
          ed_falls(i) <= ed_falls(i) + 1;
        end if;
      end loop;
      prev_s := dbg_state;
      prev_e := dbg_edge;
    end if;
  end process;

  -- Vigilante: si el DUT deja colgado el bus, la simulación termina con error.
  watchdog : process
  begin
    wait for 2 ms;
    if not done then
      report "TB_RESULT: FAIL (timeout)" severity failure;
    end if;
    wait;
  end process;

  stim : process
    variable rd     : std_logic_vector(31 downto 0);
    variable errors : natural := 0;
    variable base   : natural;

    procedure wait_clk(n : natural) is
    begin
      for i in 1 to n loop
        wait until rising_edge(clk);
      end loop;
    end procedure;

    procedure wait_ms(n : natural) is
    begin
      wait_clk(n * TPM);
    end procedure;

    procedure check(cond : boolean; msg : string) is
    begin
      if cond then
        report "ok    : " & msg severity note;
      else
        report "FALLA : " & msg severity error;
        errors := errors + 1;
      end if;
    end procedure;

    -- Escritura AXI4-Lite: AW y W juntos, luego espera la respuesta B.
    procedure axi_write(addr : natural; data : std_logic_vector(31 downto 0)) is
    begin
      awaddr  <= std_logic_vector(to_unsigned(addr, 5));
      wdata   <= data;
      wstrb   <= "1111";
      awvalid <= '1';
      wvalid  <= '1';
      bready  <= '1';
      wait until rising_edge(clk) and awready = '1';
      awvalid <= '0';
      wvalid  <= '0';
      wait until rising_edge(clk) and bvalid = '1';
      bready  <= '0';
    end procedure;

    -- Lectura AXI4-Lite.
    procedure axi_read(addr : natural; data : out std_logic_vector(31 downto 0)) is
    begin
      araddr  <= std_logic_vector(to_unsigned(addr, 5));
      arvalid <= '1';
      rready  <= '1';
      wait until rising_edge(clk) and arready = '1';
      arvalid <= '0';
      wait until rising_edge(clk) and rvalid = '1';
      data := rdata;
      rready <= '0';
    end procedure;
  begin
    ---------------------------------------------------------------- T1: reset
    rstn <= '0';
    wait_clk(5);
    rstn <= '1';
    wait_clk(2);
    axi_read(R_CTRL, rd);   check(rd = x"00000000", "T1 CTRL = 0 tras reset");
    axi_read(R_DEBMS, rd);  check(rd = x"00000014", "T1 DEB_MS = 20 tras reset");
    axi_read(R_MASK, rd);   check(rd = x"00000007", "T1 BTN_MASK = 0x7 tras reset");
    axi_read(R_STATE, rd);  check(rd = x"00000000", "T1 STATE = 0 tras reset");
    axi_read(R_EDGE, rd);   check(rd = x"00000000", "T1 EDGE = 0 tras reset");

    ------------------------------------------- T2: pulsación de S1 con rebotes
    axi_write(R_CTRL,  x"00000007");     -- EN | IRQ_EN | INV
    axi_write(R_DEBMS, x"00000005");     -- 5 ms
    axi_read(R_DEBMS, rd);  check(rd = x"00000005", "T2 DEB_MS se escribe y se relee");
    wait_ms(2);
    base := st_rises(0);
    btn_in(0) <= '0'; wait_ms(1);        -- rebotes de 1 a 3 ms (menores que DEB_MS)
    btn_in(0) <= '1'; wait_ms(1);
    btn_in(0) <= '0'; wait_ms(2);
    btn_in(0) <= '1'; wait_ms(1);
    btn_in(0) <= '0'; wait_ms(3);
    btn_in(0) <= '1'; wait_ms(1);
    check(st_rises(0) = base, "T2 STATE(0) no cambia durante los rebotes");
    btn_in(0) <= '0'; wait_ms(8);        -- ahora queda estable
    check(st_rises(0) = base + 1, "T2 una sola transicion de STATE(0)");
    axi_read(R_STATE, rd);  check(rd(2 downto 0) = "001", "T2 STATE = 001");
    axi_read(R_EDGE, rd);   check(rd(2 downto 0) = "001", "T2 EDGE = 001");
    check(irq = '1', "T2 irq = 1 con EDGE pendiente");

    ------------------------------------------------------------- T3: W1C
    axi_write(R_EDGE, x"00000001");
    wait_clk(3);
    check(irq = '0', "T3 irq baja a lo mas 3 ciclos despues del W1C");
    axi_read(R_EDGE, rd);   check(rd(2 downto 0) = "000", "T3 EDGE = 0 tras el W1C");
    axi_read(R_STATE, rd);  check(rd(2 downto 0) = "001", "T3 STATE sigue en 001 (boton mantenido)");
    btn_in(0) <= '1'; wait_ms(8);        -- soltar
    check(dbg_state(0) = '0', "T3 STATE(0) vuelve a 0 al soltar");
    axi_read(R_EDGE, rd);   check(rd(2 downto 0) = "000", "T3 soltar no genera flanco");

    ------------------------------------------------------- T4: BTN_MASK
    axi_write(R_MASK, x"00000006");      -- S1 deshabilitado
    btn_in(0) <= '0'; wait_ms(8);
    axi_read(R_STATE, rd);  check(rd(0) = '1', "T4 STATE(0) = 1 aunque este enmascarado");
    axi_read(R_EDGE, rd);   check(rd(0) = '0', "T4 EDGE(0) = 0 con BTN_MASK(0) = 0");
    check(irq = '0', "T4 sin irq");
    btn_in(0) <= '1'; wait_ms(8);
    axi_write(R_MASK, x"00000007");

    ------------------------------------------------------- T5: IRQ_EN = 0
    axi_write(R_CTRL, x"00000005");      -- EN | INV, sin IRQ_EN
    btn_in(0) <= '0'; wait_ms(8);
    axi_read(R_EDGE, rd);   check(rd(0) = '1', "T5 EDGE(0) = 1 con IRQ_EN = 0");
    check(irq = '0', "T5 irq = 0 con IRQ_EN = 0");
    axi_write(R_EDGE, x"00000007");
    btn_in(0) <= '1'; wait_ms(8);

    ----------------------------------------------------------- T6: EN = 0
    axi_write(R_CTRL, x"00000004");      -- solo INV
    wait_clk(3);
    base := st_rises(0);
    btn_in(0) <= '0'; wait_ms(8);
    axi_read(R_STATE, rd);  check(rd(2 downto 0) = "000", "T6 STATE = 0 con EN = 0");
    axi_read(R_EDGE, rd);   check(rd(2 downto 0) = "000", "T6 sin flancos con EN = 0");
    check(st_rises(0) = base, "T6 STATE(0) no se mueve con EN = 0");
    btn_in(0) <= '1'; wait_ms(2);
    axi_write(R_CTRL, x"00000007");
    wait_ms(2);

    ---------------------------- T7: flanco nuevo en el mismo ciclo que el W1C
    -- 1) Dejar un flanco pendiente en EDGE(0).
    btn_in(0) <= '0'; wait_ms(8);
    btn_in(0) <= '1'; wait_ms(8);
    axi_read(R_EDGE, rd);   check(rd(0) = '1', "T7 flanco previo pendiente");
    -- 2) Presionar alineado con el tick: STATE(0) cambia en el 5.o tick siguiente.
    wait until rising_edge(clk) and dbg_tick = '1';
    btn_in(0) <= '0';
    for i in 1 to 4 loop
      wait until rising_edge(clk) and dbg_tick = '1';
    end loop;
    -- 3) Lanzar el W1C 2 ciclos antes del cambio de STATE: el pulso de borrado
    --    coincide con el pulso 'rise' del nucleo.
    wait_clk(8);
    base := ed_falls(0);
    axi_write(R_EDGE, x"00000001");
    wait_clk(5);
    check(dbg_state(0) = '1', "T7 STATE(0) = 1 (la segunda pulsacion se filtro)");
    check(ed_falls(0) = base, "T7 EDGE(0) nunca baja: gana el flanco nuevo");
    axi_read(R_EDGE, rd);   check(rd(0) = '1', "T7 EDGE(0) = 1 tras W1C + flanco simultaneos");
    axi_write(R_EDGE, x"00000007");
    btn_in(0) <= '1'; wait_ms(8);

    ------------------------------------------- T8: boton virtual desde el VIO
    btn_virt(1) <= '1'; wait_ms(8);
    check(dbg_state(1) = '1', "T8 btn_virt(1) equivale a presionar S2");
    axi_read(R_EDGE, rd);   check(rd(2 downto 0) = "010", "T8 EDGE = 010");
    check(irq = '1', "T8 irq = 1");
    btn_virt(1) <= '0'; wait_ms(8);
    axi_write(R_EDGE, x"00000007");
    wait_clk(3);
    check(irq = '0', "T8 irq = 0 tras borrar");

    ---------------------------------------------- T9: glitch < DEB_MS
    base := st_rises(2);
    btn_in(2) <= '0'; wait_ms(3);
    btn_in(2) <= '1'; wait_ms(8);
    check(st_rises(2) = base, "T9 un glitch de 3 ms no cambia STATE(2)");
    axi_read(R_EDGE, rd);   check(rd(2 downto 0) = "000", "T9 sin flancos");

    --------------------------------------------- T10: polaridad (INV = 0)
    axi_write(R_CTRL, x"00000003");      -- EN | IRQ_EN, sin INV
    wait_ms(8);
    axi_read(R_STATE, rd);  check(rd(2 downto 0) = "111", "T10 con INV = 0 el reposo se lee como presionado");
    axi_write(R_CTRL, x"00000000");

    ------------------------------------- T11: LED_CTRL (habilitador modo claro)
    axi_read(16#14#, rd);   check(rd(4 downto 0) = "01111", "T11 reset: patron 0xF, tema oscuro");
    check(leds = "0000", "T11 tema oscuro -> LEDs apagados");
    axi_write(16#14#, x"00000015");      -- THEME_LIGHT = 1, patron 0101
    wait_clk(2);
    check(leds = "0101", "T11 tema claro -> LEDs = patron");
    axi_read(16#14#, rd);   check(rd(4 downto 0) = "10101", "T11 relectura LED_CTRL = 0x15");
    axi_write(16#14#, x"00000005");      -- vuelve a oscuro
    wait_clk(2);
    check(leds = "0000", "T11 de vuelta a oscuro -> LEDs apagados");

    ---------------------------------------------------------------- fin
    if errors = 0 then
      report "TB_RESULT: PASS (T1..T11)" severity note;
    else
      report "TB_RESULT: FAIL, errores = " & integer'image(errors) severity error;
    end if;
    done <= true;
    wait;
  end process;
end architecture;
