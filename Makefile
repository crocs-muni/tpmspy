HOSTNAME = $(shell hostname -s)

.PHONY: all
all: compile_commands.json | build
	cmake -B build/$(HOSTNAME) -S .
	make -k -C build/$(HOSTNAME)

compile_commands.json: $(wildcard build/$(HOSTNAME)/compile_commands.json)
	cp $< $@

build:
	mkdir -p $@

.PHONY: all
clean:
	make -C build/$(HOSTNAME) clean
