all: split-news

NEWS:
	wget https://raw.githubusercontent.com/systemd/systemd/refs/heads/main/NEWS -O $@

.PHONY: split-news
split-news: NEWS
	./split $^

.PHONY: clean
clean:
	$(RM) NEWS-*.txt

.PHONY: distclean
distclean: clean
	$(RM) NEWS
