all: sst sstest

sst: sst.c
	$(CC) -D_KERNEL -c sst.c
	ld -r sst.o -o sst

sstest: sstest.c
	$(CC) -o sstest sstest.c

install: sst
	cp sst /usr/kernel/drv
	cp sst.conf /usr/kernel/drv
#	echo "type=sample_driver;name=sst;minor=character\trsst\A1" >> /etc/devlink.tab

clean:
	rm sst sstest
