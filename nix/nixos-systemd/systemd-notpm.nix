{
  inputs = {
    nixpkgs-systemd-250 = {
      url = "github:NixOS/nixpkgs/1715c13faa2632c0157435babda955fbc3e27cd7";
    };
  };

  outputs = {
      self,
      nixpkgs-systemd-250,
      ...
  } : {
    nixosConfigurations.systemd-250 = nixpkgs-systemd-250.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./cfg/systemd-250/configuration.nix
        # This only sets ‹-Dtpm2=false›, but we also need to set ‹-Dtpm=false›.
        # See ‹configuration.nix› for the overlay setting.
        # {
        #   systemd.package = nixpkgs-systemd-250.legacyPackages.x86_64-linux.systemd.override {
        #     withTpm2Tss = false;
        #   };
        # }
      ];
    };
  };
}
