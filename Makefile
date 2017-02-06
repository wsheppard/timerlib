
CFLAGS= --shared -fPIC

libtimerlib.so: timerlib.o
	$(CC) $(CFLAGS) $^ -o $@
