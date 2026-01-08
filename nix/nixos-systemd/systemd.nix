{
  inputs = {
    nixpkgs-systemd-244 = {
      url = "https://github.com/NixOS/nixpkgs/archive/9de0ac3770c10221b0c6ad33576f2c525544581b.tar.gz";
      flake = false;
    };
    nixpkgs-systemd-245 = {
      url = "https://github.com/NixOS/nixpkgs/archive/a7e42e64c86da96a54fec565f6403ac6c3f6adb4.tar.gz";
      flake = false;
    };
    nixpkgs-systemd-246 = {
      url = "github:NixOS/nixpkgs/f85b2a1c89c40307dae37c01671e61d30ca1fdb9";
    };
    nixpkgs-systemd-247 = {
      url = "github:NixOS/nixpkgs/c140343a04fd6b9aab86a5cc2656fa0acf1a2a01";
    };
    nixpkgs-systemd-249 = {
      url = "github:NixOS/nixpkgs/acd5e8ef32eec138e17851637d95381d55d2264b";
    };
    nixpkgs-systemd-250 = {
      url = "github:NixOS/nixpkgs/1715c13faa2632c0157435babda955fbc3e27cd7";
    };
    nixpkgs-systemd-251 = {
      url = "github:NixOS/nixpkgs/8e36f0c4d18a55630954ff2206b1c05ec3fb8bb5";
    };
    nixpkgs-systemd-252 = {
      url = "github:NixOS/nixpkgs/e57d97b2be9f67d6413fc513ebb45d65ccca5d1e";
    };
    nixpkgs-systemd-253 = {
      url = "github:NixOS/nixpkgs/351cec5db3b7ca839779316a8b7c524ea8941f0f";
    };
    nixpkgs-systemd-254 = {
      url = "github:NixOS/nixpkgs/8374ab2113c7522766acf5ab1af9d8c6824c06d4";
    };
    nixpkgs-systemd-255 = {
      url = "github:NixOS/nixpkgs/5b81448bdd6ece26a75ccbfe748112570cba3bb3";
    };
    nixpkgs-systemd-256 = {
      url = "github:NixOS/nixpkgs/f27b62e789ceae5531852d8a015bae05ada145de";
    };
    nixpkgs-systemd-257 = {
      url = "github:NixOS/nixpkgs/4a1cad4c4625355d5fcc5febbbafe07878a067f2";
    };
    nixpkgs-systemd-258 = {
      url = "github:NixOS/nixpkgs/8461a645e094ce4200d943da608d80f157245b81";
    };
  };

  outputs = {
      self,
      nixpkgs-systemd-244,
      nixpkgs-systemd-245,
      nixpkgs-systemd-246,
      nixpkgs-systemd-247,
      nixpkgs-systemd-249,
      nixpkgs-systemd-250,
      nixpkgs-systemd-251,
      nixpkgs-systemd-252,
      nixpkgs-systemd-253,
      nixpkgs-systemd-254,
      nixpkgs-systemd-255,
      nixpkgs-systemd-256,
      nixpkgs-systemd-257,
      nixpkgs-systemd-258,
      ...
  } : {
    nixosConfigurations.systemd-244 = import "${nixpkgs-systemd-244}/nixos" {
      system = "x86_64-linux";
      configuration = {
        imports = [ ./cfg/systemd-244/configuration.nix ];
        nixpkgs.config = {
          doCheck = false;
          packageOverrides = pkgs: {
            bash-completion = pkgs.bash-completion.overrideAttrs (_: {
              doCheck = false;
              checkPhase = "true";
            });
          };
        };
      };
    };
    nixosConfigurations.systemd-245 = import "${nixpkgs-systemd-245}/nixos" {
      system = "x86_64-linux";
      configuration = {
        imports = [ ./cfg/systemd-245/configuration.nix ];
        nixpkgs.config = {
          doCheck = false;
          packageOverrides = pkgs: {
            bash-completion = pkgs.bash-completion.overrideAttrs (_: {
              doCheck = false;
              checkPhase = "true";
            });
          };
        };
      };
    };
    nixosConfigurations.systemd-246 = nixpkgs-systemd-246.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./cfg/systemd-246/configuration.nix
        { systemd.package = nixpkgs-systemd-246.legacyPackages.x86_64-linux.systemd; }
      ];
    };
    nixosConfigurations.systemd-247 = nixpkgs-systemd-247.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./cfg/systemd-247/configuration.nix
        { systemd.package = nixpkgs-systemd-247.legacyPackages.x86_64-linux.systemd; }
      ];
    };
    nixosConfigurations.systemd-249 = nixpkgs-systemd-249.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./cfg/systemd-249/configuration.nix
        { systemd.package = nixpkgs-systemd-249.legacyPackages.x86_64-linux.systemd; }
      ];
    };
    nixosConfigurations.systemd-250 = nixpkgs-systemd-250.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./cfg/systemd-250/configuration.nix
        { systemd.package = nixpkgs-systemd-250.legacyPackages.x86_64-linux.systemd; }
      ];
    };
    nixosConfigurations.systemd-251 = nixpkgs-systemd-251.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./cfg/systemd-251/configuration.nix
        { systemd.package = nixpkgs-systemd-251.legacyPackages.x86_64-linux.systemd; }
      ];
    };
    nixosConfigurations.systemd-252 = nixpkgs-systemd-252.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./cfg/systemd-252/configuration.nix
        { systemd.package = nixpkgs-systemd-252.legacyPackages.x86_64-linux.systemd; }
      ];
    };
    nixosConfigurations.systemd-253 = nixpkgs-systemd-253.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./cfg/systemd-253/configuration.nix
        { systemd.package = nixpkgs-systemd-253.legacyPackages.x86_64-linux.systemd; }
      ];
    };
    nixosConfigurations.systemd-254 = nixpkgs-systemd-254.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./cfg/systemd-254/configuration.nix
        { systemd.package = nixpkgs-systemd-254.legacyPackages.x86_64-linux.systemd; }
      ];
    };
    nixosConfigurations.systemd-255 = nixpkgs-systemd-255.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./cfg/systemd-255/configuration.nix
        { systemd.package = nixpkgs-systemd-255.legacyPackages.x86_64-linux.systemd; }
      ];
    };
    nixosConfigurations.systemd-256 = nixpkgs-systemd-256.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./cfg/systemd-256/configuration.nix
        { systemd.package = nixpkgs-systemd-256.legacyPackages.x86_64-linux.systemd; }
      ];
    };
    nixosConfigurations.systemd-257 = nixpkgs-systemd-257.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./cfg/systemd-257/configuration.nix
        { systemd.package = nixpkgs-systemd-257.legacyPackages.x86_64-linux.systemd; }
      ];
    };
    nixosConfigurations.systemd-258 = nixpkgs-systemd-258.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./cfg/systemd-258/configuration.nix
        { systemd.package = nixpkgs-systemd-258.legacyPackages.x86_64-linux.systemd; }
      ];
    };
  };
}
