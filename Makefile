HOSTNAME = $(shell hostname -s)

.PHONY: all
all: compile_commands.json

compile_commands.json: $(wildcard build/$(HOSTNAME)/compile_commands.json) | compile
	cp $< $@

build:
	mkdir -p $@

.PHONY: compile
compile: build
	cmake -B build/$(HOSTNAME) -S .
	make -k -C build/$(HOSTNAME)

install: build/$(HOSTNAME)/tpmspy build/$(HOSTNAME)/swtpm
	install $^ /usr/local/bin

uninstall:
	$(RM) /usr/local/bin/{tpmspy,swtpm}

.PHONY: all
clean:
	make -C build/$(HOSTNAME) clean
