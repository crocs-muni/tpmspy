NIX.CONF = systemd.nix systemd-notpm.nix

.PHONY: all
all: $(NIX.CONF)

%.nix: %.nix.tmpl %.cfg
	./flake.pl $^ >$@

.PHONY: install
install: install-systemd install-systemd-notpm

install-%: %.nix
	ssh root@nixos-systemd.vm 'mkdir -p /root/flakes/$*'
	scp $^ "root@nixos-systemd.vm:/root/flakes/$*/flake.nix"
	rsync -rlHpAtv --delete --progress $*.d/ "root@nixos-systemd.vm:/root/flakes/$*/cfg/"

.PHONY: clean
clean:
	$(RM) $(NIX.CONF)
