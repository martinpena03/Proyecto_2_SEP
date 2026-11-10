-- =============================================================================
-- debouncer_axi.vhd - IP propio de Sep Hero: debouncer_core + registros AXI4-Lite + irq.
--
-- Sep Hero (IEE2463, Proyecto 2). Actividad AO3: IP Core que interactúa con un
-- periférico (botones S1, S2 y JOY_SEL de la BoosterPack) y con el procesador
-- (esclavo AXI4-Lite + interrupción de nivel).
-- Autores: grupo Sep Hero.
-- Fuente: diseño propio. El esclavo AXI4-Lite está escrito a mano siguiendo el
--   protocolo visto en la AYU05 (IP RGB_driver) y en el IP BUZZER_AXI de la AYU10,
--   pero sin el banco genérico de la plantilla de Vivado.
--
-- Botones: [0] S1 (OK), [1] S2 (atrás/pausa), [2] JOY_SEL (OK).
--
-- Mapa de registros (offset desde la base AXI):
--   0x00 CTRL     R/W  [0] EN, [1] IRQ_EN, [2] INV (1 = botones activos en bajo)
--   0x04 DEB_MS   R/W  [7:0] tiempo de antirrebote en ms (reset = 20)
--   0x08 BTN_MASK R/W  [2:0] botones que generan flanco (reset = 0x7)
--   0x0C STATE    R    [2:0] nivel filtrado (1 = presionado)
--   0x10 EDGE     R/W1C [2:0] flancos de presión pendientes; escribir 1 borra el bit
--
-- irq = 1 mientras EDGE /= 0 y CTRL.IRQ_EN = 1 (interrupción de nivel alto).
-- Si un flanco nuevo coincide con el W1C del mismo bit, gana el flanco: no se
-- pierden pulsaciones.
--
-- Puertos de depuración (AC5): btn_virt permite "presionar" botones desde un VIO;
-- dbg_state, dbg_edge y dbg_tick alimentan el ILA y el VIO.
-- =============================================================================
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity debouncer_axi is
  generic (
    C_S00_AXI_DATA_WIDTH : integer  := 32;
    C_S00_AXI_ADDR_WIDTH : integer  := 5;
    G_WIDTH              : positive := 3;
    G_TICKS_PER_MS       : positive := 50_000
  );
  port (
    -- Periférico: botones (asíncronos).
    btn_in          : in  std_logic_vector(G_WIDTH-1 downto 0);
    -- Botones virtuales desde el VIO, activos en alto (OR después de corregir polaridad).
    btn_virt        : in  std_logic_vector(G_WIDTH-1 downto 0) := (others => '0');
    -- Interrupción hacia el GIC (nivel alto).
    irq             : out std_logic;
    -- Depuración (ILA / VIO).
    dbg_state       : out std_logic_vector(G_WIDTH-1 downto 0);
    dbg_edge        : out std_logic_vector(G_WIDTH-1 downto 0);
    dbg_tick        : out std_logic;
    -- Esclavo AXI4-Lite.
    s00_axi_aclk    : in  std_logic;
    s00_axi_aresetn : in  std_logic;
    s00_axi_awaddr  : in  std_logic_vector(C_S00_AXI_ADDR_WIDTH-1 downto 0);
    s00_axi_awprot  : in  std_logic_vector(2 downto 0);
    s00_axi_awvalid : in  std_logic;
    s00_axi_awready : out std_logic;
    s00_axi_wdata   : in  std_logic_vector(C_S00_AXI_DATA_WIDTH-1 downto 0);
    s00_axi_wstrb   : in  std_logic_vector((C_S00_AXI_DATA_WIDTH/8)-1 downto 0);
    s00_axi_wvalid  : in  std_logic;
    s00_axi_wready  : out std_logic;
    s00_axi_bresp   : out std_logic_vector(1 downto 0);
    s00_axi_bvalid  : out std_logic;
    s00_axi_bready  : in  std_logic;
    s00_axi_araddr  : in  std_logic_vector(C_S00_AXI_ADDR_WIDTH-1 downto 0);
    s00_axi_arprot  : in  std_logic_vector(2 downto 0);
    s00_axi_arvalid : in  std_logic;
    s00_axi_arready : out std_logic;
    s00_axi_rdata   : out std_logic_vector(C_S00_AXI_DATA_WIDTH-1 downto 0);
    s00_axi_rresp   : out std_logic_vector(1 downto 0);
    s00_axi_rvalid  : out std_logic;
    s00_axi_rready  : in  std_logic
  );
end entity;

architecture rtl of debouncer_axi is
  constant W   : positive := G_WIDTH;
  constant ZW  : std_logic_vector(W-1 downto 0) := (others => '0');
  -- Alias (no señales): una señal intermedia retrasaría el reloj un delta respecto de
  -- los datos AXI y en simulación el esclavo muestrearía valores "del futuro".
  alias clk  : std_logic is s00_axi_aclk;
  alias rstn : std_logic is s00_axi_aresetn;
  -- Handshake AXI.
  signal awready, wready : std_logic := '0';
  signal bvalid          : std_logic := '0';
  signal arready, rvalid : std_logic := '0';
  signal rdata           : std_logic_vector(31 downto 0) := (others => '0');
  -- Registros.
  signal ctrl     : std_logic_vector(2 downto 0) := "000";      -- INV, IRQ_EN, EN
  signal deb_ms   : unsigned(7 downto 0) := to_unsigned(20, 8);
  signal btn_mask : std_logic_vector(W-1 downto 0) := (others => '1');
  signal edge     : std_logic_vector(W-1 downto 0) := (others => '0');
  signal w1c      : std_logic_vector(W-1 downto 0) := (others => '0');  -- pulso de borrado
  signal irq_r    : std_logic := '0';
  -- Núcleo.
  signal btn_pos, btn_mix, state, rise : std_logic_vector(W-1 downto 0);
begin
  -- Polaridad (CTRL.INV) y botones virtuales del VIO (AC5), ambos antes del sincronizador.
  btn_pos <= not btn_in when ctrl(2) = '1' else btn_in;
  btn_mix <= btn_pos or btn_virt;

  u_core : entity work.debouncer_core
    generic map (G_WIDTH => W, G_TICKS_PER_MS => G_TICKS_PER_MS)
    port map (clk => clk, rst_n => rstn, en => ctrl(0), deb_ms => deb_ms,
              btn_in => btn_mix, state => state, rise => rise, tick_ms => dbg_tick);

  -- Canal de escritura: espera AWVALID y WVALID juntos -> 1 ciclo de READY ->
  -- escribe el registro -> BVALID hasta que el maestro lo acepta con BREADY.
  p_wr : process(clk)
  begin
    if rising_edge(clk) then
      if rstn = '0' then
        awready  <= '0';
        wready   <= '0';
        bvalid   <= '0';
        ctrl     <= "000";
        deb_ms   <= to_unsigned(20, 8);
        btn_mask <= (others => '1');
        w1c      <= (others => '0');
      else
        w1c <= (others => '0');                      -- el pulso de borrado dura 1 ciclo
        if awready = '0' and s00_axi_awvalid = '1' and s00_axi_wvalid = '1' and bvalid = '0' then
          awready <= '1';
          wready  <= '1';
        else
          awready <= '0';
          wready  <= '0';
        end if;
        if awready = '1' and s00_axi_awvalid = '1' and s00_axi_wvalid = '1' then
          if s00_axi_wstrb(0) = '1' then
            case s00_axi_awaddr(4 downto 2) is
              when "000"  => ctrl     <= s00_axi_wdata(2 downto 0);
              when "001"  => deb_ms   <= unsigned(s00_axi_wdata(7 downto 0));
              when "010"  => btn_mask <= s00_axi_wdata(W-1 downto 0);
              when "100"  => w1c      <= s00_axi_wdata(W-1 downto 0);
              when others => null;                   -- STATE es solo lectura
            end case;
          end if;
          bvalid <= '1';
        elsif bvalid = '1' and s00_axi_bready = '1' then
          bvalid <= '0';
        end if;
      end if;
    end if;
  end process;

  -- Latch de flancos (W1C; si un flanco coincide con el borrado, gana el flanco) e irq.
  p_edge : process(clk)
  begin
    if rising_edge(clk) then
      if rstn = '0' then
        edge  <= (others => '0');
        irq_r <= '0';
      else
        edge <= (edge and not w1c) or (rise and btn_mask);
        if edge /= ZW and ctrl(1) = '1' then
          irq_r <= '1';
        else
          irq_r <= '0';
        end if;
      end if;
    end if;
  end process;

  -- Canal de lectura: ARVALID -> 1 ciclo de ARREADY -> RVALID con el dato.
  p_rd : process(clk)
  begin
    if rising_edge(clk) then
      if rstn = '0' then
        arready <= '0';
        rvalid  <= '0';
        rdata   <= (others => '0');
      else
        if arready = '0' and s00_axi_arvalid = '1' and rvalid = '0' then
          arready <= '1';
        else
          arready <= '0';
        end if;
        if arready = '1' and s00_axi_arvalid = '1' then
          rvalid <= '1';
          rdata  <= (others => '0');
          case s00_axi_araddr(4 downto 2) is
            when "000"  => rdata(2 downto 0)   <= ctrl;
            when "001"  => rdata(7 downto 0)   <= std_logic_vector(deb_ms);
            when "010"  => rdata(W-1 downto 0) <= btn_mask;
            when "011"  => rdata(W-1 downto 0) <= state;
            when "100"  => rdata(W-1 downto 0) <= edge;
            when others => null;
          end case;
        elsif rvalid = '1' and s00_axi_rready = '1' then
          rvalid <= '0';
        end if;
      end if;
    end if;
  end process;

  s00_axi_awready <= awready;
  s00_axi_wready  <= wready;
  s00_axi_bresp   <= "00";
  s00_axi_bvalid  <= bvalid;
  s00_axi_arready <= arready;
  s00_axi_rdata   <= rdata;
  s00_axi_rresp   <= "00";
  s00_axi_rvalid  <= rvalid;
  irq       <= irq_r;
  dbg_state <= state;
  dbg_edge  <= edge;
end architecture;
