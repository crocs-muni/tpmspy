# Comparing TPMSpy for a nixos-systemd and nixos-crypted hosts

Assuming we have `$SYSTEMD_JSON` and `$CRYPTED_JSON` files from TPMSpy.

* Check whether these runs differ (they most likely will be):

	py/hashcheck.py [--diff] $SYSTEMD_JSON $CRYPTED_JSON

* Check number of writes to PCR registers:

	for file in $SYSTEMD_JSON $CRYPTED_JSON:
		py/pcrstat.py $file

* Compare graphs; `--end 10.0` or more may be required to account for delay
  when writing disk password:

		py/single.py --end 10.0 $SYSTEMD_JSON $SYSTEMD_SVG
		py/single.py --end 10.0 $CRYPTED_JSON $CRYPTED_SVG

  Open `$SYSTEMD_SVG` and `$CRYPTED_SVG` in image viewer that understand SVG
  (like `ristretto`).
