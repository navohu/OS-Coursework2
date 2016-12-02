CFLAGS = -g -Wall
ALL:	dos_ls dos_cp

main: main.o dos.o
	$(CC) $(CFLAGS) -o main main.o

dos_ls:	dos_ls.o dos.o
	$(CC) $(CFLAGS) -o dos_ls dos_ls.o dos.o

dos_cp:	dos_cp.o dos.o
	$(CC) $(CFLAGS) -o dos_cp dos_cp.o dos.o
