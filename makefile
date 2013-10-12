all: 
	gcc -g *.c -lm -lpthread -o Z502.exe
clean:
	rm -rf *.exe Z502.exe.dSYM 
