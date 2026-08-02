#pragma once

// Console — the `sc` console command (P5, user-decided one-letter-prefix
// grammar): `sc <mode>` switches modes, `sc <tool> <arg>` runs a tool
// subcommand, bare `sc` prints the current mode + usage.
//
//   sc mk | del | pk | pl | ed | off     switch mode
//   sc mk dp0 / sc mk dp1                hide / show every marker gem
//
// Implementation: repurposes ("hijacks") an inert vanilla ObScript console
// command — the established SKSE pattern; the engine has no API to ADD a
// console command, but SCRIPT_FUNCTION::LocateConsoleCommand hands us a
// mutable entry in the static command table. Donor candidates are debug
// commands that do nothing in retail SE; the first one present is taken and
// logged. If none resolves (or another mod already took it), the panel's
// Settings page still switches modes — the console is a convenience layer.

namespace Console {

    void Install();  // call once at kDataLoaded

}  // namespace Console
