all:	nascom

nascom:	main.o memory.o ports.o z80-simulator.o
#nascom:	main.o memory.o ports.o simz80.o
		g++ $^ -o $@

%.o:	%.cpp
		g++ -c $< -o $@

clean:
	rm -rf *.o nascom
