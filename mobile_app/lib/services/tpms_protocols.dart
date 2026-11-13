/// Maps a decoded TPMS protocol id to a human description.
///
/// Sourced from the public rtl_433 decoder notes (protocol → manufacturer +
/// vehicle families). NOTE: a protocol narrows the *make*, not the exact car —
/// per-make frequency is unreliable (e.g. Ford is mostly 315 MHz but some 2018
/// are 433), so we describe the sensor family, not a specific vehicle.
class TpmsProtocolInfo {
  final String name; // short label
  final String manufacturer; // sensor maker
  final String makes; // vehicle families it's seen on
  const TpmsProtocolInfo(this.name, this.manufacturer, this.makes);
}

const Map<int, TpmsProtocolInfo> kTpmsProtocols = {
  0: TpmsProtocolInfo('Unidentified', 'unknown', 'protocol not yet decoded'),
  1: TpmsProtocolInfo(
      'Ford', 'Continental / VDO', 'Ford: Fiesta, Focus, Kuga, Escape, Transit…'),
  2: TpmsProtocolInfo(
      'Toyota', 'Pacific Industries', 'Toyota, Lexus, Subaru, Scion…'),
  3: TpmsProtocolInfo('Hyundai / VDO', 'Continental / VDO',
      'Hyundai, Kia — also BMW, Fiat/Chrysler, Peugeot/Citroën, Mazda, Mitsubishi'),
  4: TpmsProtocolInfo(
      'Schrader', 'Schrader', 'many Schrader-equipped vehicles (FCC MRXGG4)'),
  5: TpmsProtocolInfo('Schrader (GM)', 'Schrader',
      'GM / Chevy (2006–2025), Opel / Saab / Vauxhall'),
};

TpmsProtocolInfo tpmsInfo(int protocol) =>
    kTpmsProtocols[protocol] ?? kTpmsProtocols[0]!;

/// kPa → PSI for display.
double kpaToPsi(int kpa) => kpa / 6.89476;
