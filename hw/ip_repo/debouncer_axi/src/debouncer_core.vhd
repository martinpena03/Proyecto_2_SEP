-- =============================================================================
-- debouncer_core.vhd - Antirrebote de N botones con tiempo configurable en runtime.
--
-- Sep Hero (IEE2463, Proyecto 2). IP propio del grupo (actividad AO3).
-- Autores: grupo Sep Hero.
-- Fuente: basado en debouncer.vhd del Proyecto 1 del grupo (P1_FInal/debouncer:
--   contador de estabilidad por botón). Cambios de este proyecto:
--     * el genérico STABLE_TIME se reemplaza por la entrada deb_ms (registro AXI)
--       y un "tick" de 1 ms, así el tiempo se cambia desde el procesador;
--     * se agrega el sincronizador de 2 FF marcado ASYNC_REG;
--     * se agrega el detector de flanco de subida (rise).
--
-- Funcionamiento: state(i) copia la entrada sincronizada cuando esta difiere de
-- state(i) durante deb_ms ticks de 1 ms SEGUIDOS. Cualquier rebote (la entrada
-- vuelve a ser igual al estado) reinicia la cuenta.
-- =============================================================================
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity debouncer_core is
  generic (
    G_WIDTH        : positive := 3;          -- cantidad de botones
    G_TICKS_PER_MS : positive := 50_000      -- ciclos de reloj por ms (FCLK_CLK0 = 50 MHz)
  );
  port (
    clk     : in  std_logic;
    rst_n   : in  std_logic;                 -- reset síncrono activo en bajo
    en      : in  std_logic;                 -- CTRL.EN: en 0 fuerza state = 0
    deb_ms  : in  unsigned(7 downto 0);      -- registro DEB_MS (0 se trata como 1)
    btn_in  : in  std_logic_vector(G_WIDTH-1 downto 0);  -- asíncrono, ya en lógica positiva
    state   : out std_logic_vector(G_WIDTH-1 downto 0);  -- nivel filtrado
    rise    : out std_logic_vector(G_WIDTH-1 downto 0);  -- pulso de 1 ciclo por flanco 0->1
    tick_ms : out std_logic                  -- pulso de 1 ciclo cada 1 ms
  );
end entity;

architecture rtl of debouncer_core is
  -- Prescaler de 1 ms.
  signal presc        : natural range 0 to G_TICKS_PER_MS-1 := 0;
  signal tick         : std_logic := '0';
  -- Sincronizador de 2 etapas para las entradas asíncronas.
  signal sync0, sync1 : std_logic_vector(G_WIDTH-1 downto 0) := (others => '0');
  -- Estado filtrado actual y del ciclo anterior (para el detector de flanco).
  signal st, st_d     : std_logic_vector(G_WIDTH-1 downto 0) := (others => '0');
  -- Un contador de estabilidad por botón, en ticks de 1 ms.
  type cnt_t is array (0 to G_WIDTH-1) of unsigned(7 downto 0);
  signal cnt          : cnt_t := (others => (others => '0'));
  attribute ASYNC_REG : string;
  attribute ASYNC_REG of sync0 : signal is "TRUE";
  attribute ASYNC_REG of sync1 : signal is "TRUE";
begin
  -- Prescaler: un pulso 'tick' por milisegundo.
  p_tick : process(clk)
  begin
    if rising_edge(clk) then
      if rst_n = '0' then
        presc <= 0;
        tick  <= '0';
      elsif presc = G_TICKS_PER_MS-1 then
        presc <= 0;
        tick  <= '1';
      else
        presc <= presc + 1;
        tick  <= '0';
      end if;
    end if;
  end process;

  -- Sincronizador de 2 FF + contador de estabilidad por botón.
  p_deb : process(clk)
  begin
    if rising_edge(clk) then
      sync0 <= btn_in;
      sync1 <= sync0;
      if rst_n = '0' or en = '0' then
        st   <= (others => '0');
        st_d <= (others => '0');
        cnt  <= (others => (others => '0'));
      else
        st_d <= st;
        for i in 0 to G_WIDTH-1 loop
          if sync1(i) = st(i) then
            cnt(i) <= (others => '0');             -- entrada igual al estado: nada que filtrar
          elsif tick = '1' then
            if cnt(i) + 1 >= deb_ms then           -- estable DEB_MS ms seguidos
              st(i)  <= sync1(i);
              cnt(i) <= (others => '0');
            else
              cnt(i) <= cnt(i) + 1;
            end if;
          end if;
        end loop;
      end if;
    end if;
  end process;

  state   <= st;
  rise    <= st and not st_d;                      -- flanco de presión (0 -> 1)
  tick_ms <= tick;
end architecture;
