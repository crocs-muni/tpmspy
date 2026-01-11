#!/usr/bin/perl

use v5.40;
use open ':std', ':encoding(utf-8)';


=encoding utf-8

=head1 NAME

C<flake.pl> -- Generate flake.nix for various systemd versions

=head1 SYNOPSIS

	flake.pl systemd.tmpl systemd.cfg | tee flake.nix

=cut


use Text::Xslate;
use YAML::PP;


sub read_file($name) {
	open my $fh, '<:encoding(utf-8)', $name
		or die "open $name: $!\n";

	return <$fh> if wantarray;
	return join '', <$fh>;
}

sub cfg_normalize($cfg) {
	foreach my ($key, $value) ($cfg->%*) {
		next if ref $value eq 'HASH';
		$value = {
			commit => $value,
			mode => 'flake',
		};
	}

	return $cfg;
}


if (@ARGV != 2) {
	say STDERR "usage: $0 TEMPLATE CONFIG";
	exit 1;
}

my ($tmpl_name, $config_name) = (@ARGV);

my $yaml = YAML::PP->new;
my $config = cfg_normalize($yaml->load_string(scalar read_file($config_name)));

my $sorted_config = [
	map { +{ version => $_, $config->{$_}->%* } } sort keys $config->%*,
];

my $tx = Text::Xslate->new(type => 'text');
print $tx->render_string(scalar(read_file($tmpl_name)), { config => $sorted_config });
