(import
  (builtins.fetchTarball https://github.com/Sorixelle/pebble.nix/archive/master.tar.gz)
).buildPebbleApp {
  name = "OpenMensa";
  src = ./.;
  type = "watchapp";

  description = ''
    Your local Mensa, right on your Pebble!
  '';

  releaseNotes = ''
    Initial release.
  '';
}
