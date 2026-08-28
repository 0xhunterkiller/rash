COMPILER=gcc
OPTIONS=-o target/rash
COMPILE=$(COMPILER) $(OPTIONS)

build:
	@echo "building rash"
	@mkdir -p target
	@$(COMPILE) shell.c

install: build
	@echo "installing rash"
	@sudo mv target/rash /usr/local/bin/rash

clean: 
	@rm -rf target/
	@rm a.out