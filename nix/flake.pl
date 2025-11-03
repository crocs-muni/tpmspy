#!env perl

=encoding utf-8

=head1 NAME

C<flake.pl> -- Generate flake.nix for various systemd versions

=head1 SYNOPSIS

	flake.pl systemd.cfg | tee flake.nix

=cut


use v5.40;
use open ':std', ':encoding(utf-8)';


use Text::Xslate;
use YAML::PP;


my $yaml = YAML::PP->new;
my $config = $yaml->load_string(join '', <>);

my $sorted_config = [
	map { +{ k => $_, v => $config->{$_} } } sort keys $config->%*,
];

my $tx = Text::Xslate->new(type => 'text');
print $tx->render_string(join('', <DATA>), { config => $sorted_config });

__DATA__
{
	inputs = {
		: for $config -> $item {
		nixpkgs-systemd-<: $item.k :>.url = "github:NixOS/nixpkgs/<: $item.v :>";
		: }
	};

	outputs = {
			self,
			: for $config -> $item {
			nixpkgs-systemd-<: $item.k :>,
			: }
			... } : {
		nixosConfigurations =
			let systems = {
				: for $config -> $item {
				"<: $item.k :>" = nixpkgs-systemd-<: $item.k :>;
				: }
			};

			systemd = version:
				systems.${version}.lib.nixosSystem {
					system = "x86_64-linux";
					modules = [
						./systemd-${version}/configuration.nix
						{ systemd.package = systems.${version}.legacyPackages.x86_64-linux.systemd; }
						{
							nixpkgs.overlays = [
								(final: prev: {
									systemd = prev.systemd.override (
										prev.lib.optionalAttrs (prev.lib.versionAtLeast prev.systemd.version "249") {
											withTpm2Tss = true;
										}
									);
								})
							];
						}
					];
				};

			in {
				: for $config -> $item {
				systemd-<: $item.k :> = systemd "<: $item.k :>";
				: }
			};
	};
}

