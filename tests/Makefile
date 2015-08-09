test: output output/md1 output/md2
	./run_tests.sh "$(LUMAIL)"

clean:
	rm -r output

output:
	mkdir -p $@

output/md1: output
	mkdir -p $@
	mkdir -p $@/cur
	mkdir -p $@/new
	mkdir -p $@/tmp

output/md2: output
	mkdir -p $@
	mkdir -p $@/cur
	mkdir -p $@/new
	mkdir -p $@/tmp
