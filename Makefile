all: producer consumer
producer: producer.cpp
		g++ -std=c++11 producer.cpp -o producer -pthread
consumer: consumer.cpp
		g++ -std=c++11 consumer.cpp -o consumer -pthread
clean:
		rm -f producer consumer
remove_shm:
		rm -f /dev/shm/my_shm
