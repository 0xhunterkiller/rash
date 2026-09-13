COMPILER=gcc
OPTIONS=-o target/rash -Wall -Wextra
COMPILE=$(COMPILER) $(OPTIONS)

build:
	@echo "building rash"
	@mkdir -p target
	@$(COMPILE) src/*.c -ltomlc17 $(OPTIONS)

install: build
	@echo "installing rash"
	@sudo mv target/rash /usr/local/bin/rash

clean: 
	@rm -rf target/
	@rm a.out