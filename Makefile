CCFLAGS= -g

all: routing_server node

routing_server: print_lib.c routing_server.c
	gcc $^ $(CCFLAGS) -o routing_server

node: print_lib.c node.c 
	gcc $^ $(CCFLAGS) -o node

run: all
	bash run_1.sh 3000
	bash run_2.sh 5000

clean:
	rm -f routing_server
	rm -f node
